// SPDX-License-Identifier: Apache-2.0
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include <hid/app/mouse.hpp>
#include <hid/application.hpp>
#include <magic_enum.hpp>
#include <port/zephyr/udc_mac.hpp>
#include <port/zephyr/usb_shell.hpp>
#include <usb/df/device.hpp>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static uint8_t serial_number[16]{};
constexpr usb::product_info product_info{CONFIG_DEMO_MANUFACTURER_ID, CONFIG_DEMO_MANUFACTURER,
                                         CONFIG_DEMO_PRODUCT_ID,      CONFIG_DEMO_PRODUCT,
                                         usb::version("1.0"),         serial_number};

auto& mac()
{
    static usb::zephyr::udc_mac mac{DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 128};
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
            using event = enum usb::df::device::event;
            if (ev == event::CONFIGURATION_CHANGE)
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
        constexpr auto config_header =
            usb::df::config::header(usb::df::config::power::bus(500, true), "base config");

        static const auto base_config = usb::df::config::make_config(
            config_header,
            usb::df::cdc::config(
                usb::zephyr::usb_shell::handle(), speed, usb::endpoint::address(0x01),
                usb::endpoint::address(0x81),
                usb::endpoint::address(0x82) // note that notification endpoint is unused here
                ));
        device().set_config(base_config);
        device().open();
    }

    while (true)
    {
        k_sleep(K_FOREVER);
    }
}
