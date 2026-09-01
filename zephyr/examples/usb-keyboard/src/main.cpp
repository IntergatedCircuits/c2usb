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
using namespace zephyr;

static const uint8_t caps_led = 0;

auto& input_msgq()
{
    static message_queue_instance<input_event, 2> msgq;
    return msgq;
}

static void input_cb(input_event* evt, void*)
{
    input_msgq().post(*evt);
}

INPUT_CALLBACK_DEFINE(nullptr, input_cb, nullptr);

#if CONFIG_HWINFO
static uint8_t serial_number[CONFIG_HWINFO_DEVICE_ID_LENGTH]{};
#endif
constexpr usb::product_info product_info{CONFIG_DEMO_MANUFACTURER_ID,
                                         CONFIG_DEMO_MANUFACTURER,
                                         CONFIG_DEMO_PRODUCT_ID,
                                         CONFIG_DEMO_PRODUCT,
                                         usb::version("1.0")
#if CONFIG_HWINFO
                                             ,
                                         serial_number
#endif
};

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
#if CONFIG_HWINFO
    // use HW info as serial number
    hwinfo_get_device_id(serial_number, sizeof(serial_number));
#endif

    simple_keyboard::instance().set_leds_callback(
        [](keyboard_leds_data leds)
        { leds::set(caps_led, leds.test(hid::page::leds::CAPS_LOCK)); });

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

    // single class function instance
    static usb::df::hid::function usb_kb{simple_keyboard::instance(), "keyboard",
                                         hid::boot::mode::KEYBOARD};

    // define configurations and start device
    {
        constexpr auto speed = usb::speed::FULL;
        constexpr auto config_header = usb::df::config::header(
            usb::df::config::power::bus(500, true), "base config but make it longer.");


        static const auto base_config = usb::df::config::make_config(
            config_header, usb_kb.config_entry(speed, usb::endpoint::address(0x81), 1
#if CONFIG_DEMO_USB_HID_OUT_EP
                                               ,
                                               usb::endpoint::address(0x01), 10
#endif
                                               ));
        device().set_config(base_config);
        device().open();
    }

    while (true)
    {
        auto msg = input_msgq().get();
        if ((msg.value) && device().power_state() == usb::power::state::L2_SUSPEND)
        {
            device().remote_wakeup();
        }
        switch (msg.code)
        {
        case INPUT_KEY_0:
            simple_keyboard::instance().send_key(hid::page::keyboard_keypad::KEYBOARD_CAPS_LOCK,
                                                 msg.value);
            break;
        case INPUT_KEY_1:
            simple_keyboard::instance().send_key(
                hid::page::keyboard_keypad::KEYBOARD_BACKSLASH_PIPE, msg.value);
            break;
        case INPUT_KEY_2:
            simple_keyboard::instance().send_key(hid::page::keyboard_keypad::KEYBOARD_ENTER,
                                                 msg.value);
            break;
        case INPUT_KEY_3:
            simple_keyboard::instance().send_key(hid::page::keyboard_keypad::KEYBOARD_F1,
                                                 msg.value);
            break;
        default:
            break;
        }
    }
}
