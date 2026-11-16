/*
 * Copyright (c) 2026 The c2usb contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <array>
#include <cstring>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include <usb/df/device.hpp>
#include <usb/df/vendor/zephyr/udc_mac.hpp>

LOG_MODULE_REGISTER(udc_power_state, LOG_LEVEL_INF);

static constexpr size_t STATE_LOG_SIZE = 16;
static constexpr auto STATE_TIMEOUT = K_SECONDS(5);
static constexpr auto STATE_SETTLE_TIME = K_MSEC(100);

static constexpr usb::product_info product_info{0x2fe3, "Zephyr", 0x00e2, "c2usb power state test",
                                                usb::version("1.0")};

static struct k_spinlock state_lock;
static struct k_sem state_seen;
static std::array<usb::power::state, STATE_LOG_SIZE> state_log;
static size_t state_count;
static bool state_overflow;

static const char* power_state_name(usb::power::state state)
{
    switch (state)
    {
    case usb::power::state::L0_ON:
        return "L0_ON";
    case usb::power::state::L1_SLEEP:
        return "L1_SLEEP";
    case usb::power::state::L2_SUSPEND:
        return "L2_SUSPEND";
    case usb::power::state::L3_OFF:
        return "L3_OFF";
    default:
        return "UNKNOWN";
    }
}

static const char* power_state_code(usb::power::state state)
{
    switch (state)
    {
    case usb::power::state::L0_ON:
        return "0";
    case usb::power::state::L1_SLEEP:
        return "1";
    case usb::power::state::L2_SUSPEND:
        return "2";
    case usb::power::state::L3_OFF:
        return "3";
    default:
        return "?";
    }
}

static const char* power_event_name(usb::df::device::event event)
{
    switch (event)
    {
    case usb::df::device::event::POWER_STATE_CHANGE:
        return "POWER_STATE_CHANGE";
    case usb::df::device::event::CONFIGURATION_CHANGE:
        return "CONFIGURATION_CHANGE";
    default:
        return "NONE";
    }
}

static auto& usb_device()
{
    static usb::df::zephyr::udc_mac mac{DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 128};
    static usb::df::device_instance<mac.supported_speeds()> device{mac, product_info};
    return device;
}

static void state_log_clear()
{
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    state_count = 0;
    state_overflow = false;
    k_spin_unlock(&state_lock, key);

    while (k_sem_take(&state_seen, K_NO_WAIT) == 0)
    {
    }
}

static size_t state_log_snapshot(usb::power::state* states, bool* overflow)
{
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    size_t count = state_count;
    std::memcpy(states, state_log.data(), count * sizeof(states[0]));
    *overflow = state_overflow;
    k_spin_unlock(&state_lock, key);
    return count;
}

static int cmd_power(const struct shell* shell, size_t argc, char** argv)
{
    usb::power::state actual[STATE_LOG_SIZE];
    bool overflow;
    size_t actual_count;

    if (argc == 2 && strcmp(argv[1], "clear") == 0)
    {
        state_log_clear();
        shell_print(shell, "OK cleared");
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "state") == 0)
    {
        shell_print(shell, "OK state=%s", power_state_name(usb_device().power_state()));
        return 0;
    }

    if (argc < 3 || strcmp(argv[1], "assert") != 0)
    {
        shell_error(shell, "Usage: power clear|state|assert <states...>");
        return -EINVAL;
    }

    if (argc - 2 > STATE_LOG_SIZE)
    {
        shell_error(shell, "Too many expected states");
        return -EINVAL;
    }

    while (true)
    {
        actual_count = state_log_snapshot(actual, &overflow);
        if (actual_count >= argc - 2 || overflow)
        {
            break;
        }

        if (k_sem_take(&state_seen, STATE_TIMEOUT) != 0)
        {
            shell_error(shell, "Timed out waiting for power state events; got %u",
                        (unsigned int)actual_count);
            return -ETIMEDOUT;
        }
    }

    k_sleep(STATE_SETTLE_TIME);
    actual_count = state_log_snapshot(actual, &overflow);
    if (overflow)
    {
        shell_error(shell, "Power state recorder overflowed");
        return -ENOBUFS;
    }

    if (actual_count != argc - 2)
    {
        for (size_t idx = 0; idx < actual_count; idx++)
        {
            shell_print(shell, "Recorded state %u: %s", (unsigned int)idx,
                        power_state_name(actual[idx]));
        }
        shell_error(shell, "Expected %u states, recorded %u", (unsigned int)(argc - 2),
                    (unsigned int)actual_count);
        return -EIO;
    }

    for (size_t idx = 0; idx < actual_count; idx++)
    {
        if ((strcmp(argv[idx + 2], power_state_name(actual[idx])) != 0) and
            (strcmp(argv[idx + 2], power_state_code(actual[idx])) != 0))
        {
            shell_error(shell, "State %u: expected %s, got %s", (unsigned int)idx, argv[idx + 2],
                        power_state_name(actual[idx]));
            return -EIO;
        }
    }

    shell_print(shell, "PASS power_states=%u", (unsigned int)actual_count);
    return 0;
}

SHELL_CMD_ARG_REGISTER(power, NULL, "Inspect c2usb power-state transitions", cmd_power, 2,
                       STATE_LOG_SIZE + 1);

int main(void)
{
    k_sem_init(&state_seen, 0, STATE_LOG_SIZE);

    auto& device = usb_device();
    device.set_power_event_delegate(
        [](usb::df::device& device, usb::df::device::event event)
        {
            auto state = device.power_state();
            k_spinlock_key_t key = k_spin_lock(&state_lock);
            if (state_count < state_log.size())
            {
                state_log[state_count++] = state;
            }
            else
            {
                state_overflow = true;
            }
            k_spin_unlock(&state_lock, key);

            LOG_INF("USB %s: state=%s open=%u", power_event_name(event), power_state_name(state),
                    device.is_open());
            k_sem_give(&state_seen);
        });
    device.open();

    LOG_INF("c2usb UDC power-state fixture ready: state=%s",
            power_state_name(device.power_state()));

    while (true)
    {
        k_sleep(K_FOREVER);
    }
}
