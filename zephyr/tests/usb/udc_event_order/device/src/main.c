/*
 * Copyright (c) 2026 The c2usb contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/spinlock.h>
#include <zephyr/usb/usbd.h>
#include <soc.h>
#include <hal/nrf_power.h>

#define EVENT_LOG_SIZE 32
#define EVENT_TIMEOUT K_SECONDS(5)
#define EVENT_SETTLE_TIME K_MSEC(100)

enum recorded_event
{
    RECORDED_VBUS_READY,
    RECORDED_VBUS_REMOVED,
    RECORDED_RESET,
    RECORDED_SUSPEND,
    RECORDED_RESUME,
};

static const char* const event_names[] = {
    [RECORDED_VBUS_READY] = "vbus_ready", [RECORDED_VBUS_REMOVED] = "vbus_removed",
    [RECORDED_RESET] = "reset",           [RECORDED_SUSPEND] = "suspend",
    [RECORDED_RESUME] = "resume",
};

USBD_DEVICE_DEFINE(event_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0x2fe3, 0x00e1);
USBD_DESC_LANG_DEFINE(event_lang);
USBD_DESC_MANUFACTURER_DEFINE(event_manufacturer, "Zephyr");
USBD_DESC_PRODUCT_DEFINE(event_product, "UDC event order test");
USBD_DESC_CONFIG_DEFINE(event_config_desc, "Configuration");
USBD_CONFIGURATION_DEFINE(event_config, 0, 100, &event_config_desc);

static struct k_spinlock event_lock;
static struct k_sem event_seen;
static enum recorded_event event_log[EVENT_LOG_SIZE];
static size_t event_count;
static bool event_overflow;

static bool record_event(enum usbd_msg_type type)
{
    switch (type)
    {
    case USBD_MSG_VBUS_READY:
        return true;
    case USBD_MSG_VBUS_REMOVED:
        return true;
    case USBD_MSG_RESET:
        return true;
    case USBD_MSG_SUSPEND:
        return true;
    case USBD_MSG_RESUME:
        return true;
    default:
        return false;
    }
}

static enum recorded_event event_from_message(enum usbd_msg_type type)
{
    switch (type)
    {
    case USBD_MSG_VBUS_READY:
        return RECORDED_VBUS_READY;
    case USBD_MSG_VBUS_REMOVED:
        return RECORDED_VBUS_REMOVED;
    case USBD_MSG_RESET:
        return RECORDED_RESET;
    case USBD_MSG_SUSPEND:
        return RECORDED_SUSPEND;
    case USBD_MSG_RESUME:
    default:
        return RECORDED_RESUME;
    }
}

static void event_callback(struct usbd_context* const ctx, const struct usbd_msg* const msg)
{
    k_spinlock_key_t key;

    ARG_UNUSED(ctx);

    if (!record_event(msg->type))
    {
        return;
    }

    key = k_spin_lock(&event_lock);
    if (event_count < ARRAY_SIZE(event_log))
    {
        event_log[event_count++] = event_from_message(msg->type);
    }
    else
    {
        event_overflow = true;
    }
    k_spin_unlock(&event_lock, key);

    k_sem_give(&event_seen);
}

static void event_log_clear(void)
{
    k_spinlock_key_t key = k_spin_lock(&event_lock);

    event_count = 0;
    event_overflow = false;
    k_spin_unlock(&event_lock, key);

    while (k_sem_take(&event_seen, K_NO_WAIT) == 0)
    {
    }
}

static size_t event_log_snapshot(enum recorded_event* events, bool* overflow)
{
    k_spinlock_key_t key = k_spin_lock(&event_lock);
    size_t count = event_count;

    memcpy(events, event_log, count * sizeof(events[0]));
    *overflow = event_overflow;
    k_spin_unlock(&event_lock, key);

    return count;
}

static int cmd_events(const struct shell* shell, size_t argc, char** argv)
{
    enum recorded_event actual[EVENT_LOG_SIZE];
    bool overflow;
    size_t actual_count;
    uint32_t usb_ints;
    uint32_t usb_events;

    if (argc == 2 && strcmp(argv[1], "status") == 0)
    {
        actual_count = event_log_snapshot(actual, &overflow);
        usb_ints = nrf_power_int_enable_get(NRF_POWER) &
                   (NRF_POWER_INT_USBDETECTED_MASK | NRF_POWER_INT_USBREMOVED_MASK |
                    NRF_POWER_INT_USBPWRRDY_MASK);
        usb_events = (nrf_power_event_check(NRF_POWER, NRF_POWER_EVENT_USBDETECTED) ? BIT(0) : 0) |
                     (nrf_power_event_check(NRF_POWER, NRF_POWER_EVENT_USBREMOVED) ? BIT(1) : 0) |
                     (nrf_power_event_check(NRF_POWER, NRF_POWER_EVENT_USBPWRRDY) ? BIT(2) : 0);
        shell_print(shell, "OK VBUS=%u READY=%u INT=%x EVT=%x",
                    nrf_power_usbregstatus_vbusdet_get(NRF_POWER),
                    nrf_power_usbregstatus_outrdy_get(NRF_POWER), usb_ints, usb_events);
        shell_print(shell, "OK LOG=%u LAST=%s", (unsigned int)actual_count,
                actual_count > 0 ? event_names[actual[actual_count - 1]] : "none");
        for (size_t idx = 0; idx < actual_count; idx++)
        {
            shell_print(shell, "LOG[%u]=%s", (unsigned int)idx, event_names[actual[idx]]);
        }
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "clear") == 0)
    {
        event_log_clear();
        shell_print(shell, "OK cleared");
        return 0;
    }

    if (argc < 2 || strcmp(argv[1], "assert") != 0)
    {
        shell_error(shell, "Usage: events status|clear|assert <expected events...>");
        return -EINVAL;
    }

    if (argc - 2 > EVENT_LOG_SIZE)
    {
        shell_error(shell, "Too many expected events");
        return -EINVAL;
    }

    while (true)
    {
        actual_count = event_log_snapshot(actual, &overflow);
        if (actual_count >= argc - 2 || overflow)
        {
            break;
        }

        if (k_sem_take(&event_seen, EVENT_TIMEOUT) != 0)
        {
            shell_error(shell, "Timed out waiting for device events");
            return -ETIMEDOUT;
        }
    }

    k_sleep(EVENT_SETTLE_TIME);
    actual_count = event_log_snapshot(actual, &overflow);
    if (overflow)
    {
        shell_error(shell, "Event recorder overflowed");
        return -ENOBUFS;
    }

    if (actual_count != argc - 2)
    {
        shell_error(shell, "Expected %u events, recorded %u", (unsigned int)(argc - 2),
                    (unsigned int)actual_count);
        return -EIO;
    }

    for (size_t idx = 0; idx < actual_count; idx++)
    {
        if (strcmp(argv[idx + 2], event_names[actual[idx]]) != 0)
        {
            shell_error(shell, "Event %u: expected %s, got %s", (unsigned int)idx, argv[idx + 2],
                        event_names[actual[idx]]);
            return -EIO;
        }
    }

    shell_print(shell, "PASS events=%u", (unsigned int)actual_count);
    return 0;
}

SHELL_CMD_ARG_REGISTER(events, NULL, "Inspect the UDC event log", cmd_events, 2, 32);

int main(void)
{
    int ret;

    k_sem_init(&event_seen, 0, EVENT_LOG_SIZE);

    ret = usbd_add_descriptor(&event_usbd, &event_lang);
    if (ret != 0)
    {
        return ret;
    }
    ret = usbd_add_descriptor(&event_usbd, &event_manufacturer);
    if (ret != 0)
    {
        return ret;
    }
    ret = usbd_add_descriptor(&event_usbd, &event_product);
    if (ret != 0)
    {
        return ret;
    }
    ret = usbd_add_configuration(&event_usbd, USBD_SPEED_FS, &event_config);
    if (ret != 0)
    {
        return ret;
    }
    ret = usbd_msg_register_cb(&event_usbd, event_callback);
    if (ret != 0)
    {
        return ret;
    }
    ret = usbd_init(&event_usbd);
    if (ret != 0)
    {
        return ret;
    }
    ret = usbd_enable(&event_usbd);
    if (ret != 0)
    {
        return ret;
    }

    while (true)
    {
        k_sleep(K_FOREVER);
    }

    return 0;
}
