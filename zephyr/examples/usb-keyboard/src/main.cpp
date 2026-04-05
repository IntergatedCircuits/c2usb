// SPDX-License-Identifier: Apache-2.0
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>

#include "../../iolib.hpp"
#include <port/zephyr/udc_mac.hpp>
#include <simple_keyboard.hpp>
#include <usb/df/class/hid.hpp>
#include <usb/df/device.hpp>
#include <zephyr/message_queue.hpp>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

using namespace magic_enum::bitwise_operators;

auto& kb_msgq()
{
    static zephyr::message_queue_instance<input_event, 2> msgq;
    return msgq;
}

static void input_cb(input_event* evt, void*)
{
    kb_msgq().post(*evt);
}

INPUT_CALLBACK_DEFINE(nullptr, input_cb, nullptr);

auto& keyboard_app()
{
    static simple_keyboard<> keyb{[](const simple_keyboard<>::kb_leds_report& report)
                                  {
                                      leds::set(0, report.leds.test(hid::page::leds::CAPS_LOCK));
                                  }};
    return keyb;
}

static uint8_t serial_number[16]{};
constexpr usb::product_info product_info{CONFIG_DEMO_MANUFACTURER_ID, CONFIG_DEMO_MANUFACTURER,
                                         CONFIG_DEMO_PRODUCT_ID,      CONFIG_DEMO_PRODUCT,
                                         usb::version("1.0"),         serial_number};

auto& mac()
{
    static usb::zephyr::udc_mac mac{DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 400};
    return mac;
}

auto& device()
{
    static usb::df::device_instance<usb::speed::FULL> device{mac(), product_info};
    return device;
}

//[[noreturn]]
int main(void)
{
    // observing device state
    device().set_power_event_delegate(
        [](usb::df::device& dev, usb::df::device::event ev)
        {
            if (ev == usb::df::device::event::CONFIGURATION_CHANGE)
            {
                LOG_INF("USB configured: %u, granted current: %uuA", (unsigned)dev.configured(),
                        dev.granted_bus_current_uA());
            }
            else
            {
                LOG_INF("USB power state: %s, granted current: %uuA",
                        magic_enum::enum_name(dev.power_state()).data(),
                        dev.granted_bus_current_uA());
            }
        });

    // use HW info as serial number
    if (IS_ENABLED(CONFIG_HWINFO))
    {
        hwinfo_get_device_id(serial_number, sizeof(serial_number));
    }
    // define configuration and start device
    {
        constexpr auto speed = usb::speed::FULL;
        constexpr auto config_header = usb::df::config::header(
            usb::df::config::power::bus(500, true), "base config but make it longer.");

        static usb::df::hid::function usb_kb{keyboard_app(), "keyboard",
                                             usb::hid::boot_protocol_mode::KEYBOARD};

        static const auto base_config = usb::df::config::make_config(
            config_header, usb::df::hid::config(usb_kb, speed, usb::endpoint::address(0x81), 1
#if 0
                                                ,
                                                usb::endpoint::address(0x01), 10
#endif
                                                ));
        device().set_config(base_config);
        device().open();
    }

    while (true)
    {
        auto msg = kb_msgq().get();
        if ((msg.value) && device().power_state() == usb::power::state::L2_SUSPEND)
        {
            device().remote_wakeup();
        }
        switch (msg.code)
        {
        case INPUT_KEY_0:
            keyboard_app().send_key(hid::page::keyboard_keypad::KEYBOARD_CAPS_LOCK, msg.value);
            break;
        case INPUT_KEY_1:
            keyboard_app().send_key(hid::page::keyboard_keypad::KEYBOARD_BACKSLASH_PIPE, msg.value);
            break;
        case INPUT_KEY_2:
            keyboard_app().send_key(hid::page::keyboard_keypad::KEYBOARD_ENTER, msg.value);
            break;
        case INPUT_KEY_3:
            keyboard_app().send_key(hid::page::keyboard_keypad::KEYBOARD_F1, msg.value);
            break;
        default:
            break;
        }
    }
}
