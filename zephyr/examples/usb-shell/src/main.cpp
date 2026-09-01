// SPDX-License-Identifier: Apache-2.0
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include <hid/app/mouse.hpp>
#include <hid/application.hpp>
#include <magic_enum.hpp>
#include <memory_resource>
#include <port/zephyr/udc_mac.hpp>
#include <port/zephyr/usb_shell.hpp>
#include <usb/df/config_storage.hpp>
#include <usb/df/device.hpp>
#include <zephyr/thread.hpp>

using namespace zephyr;

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

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

auto& device()
{
    static usb::zephyr::udc_mac mac{DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 128};
    static usb::df::device_instance<mac.supported_speeds()> device{mac, product_info};
    return device;
}

//[[noreturn]]
int main(void)
{
#if CONFIG_HWINFO
    // use HW info as serial number
    hwinfo_get_device_id(serial_number, sizeof(serial_number));
#endif

    // observing device state
    device().set_power_event_delegate(
        [](usb::df::device& dev, usb::df::device::event ev)
        {
            using event = enum usb::df::device::event;
            if (ev == event::CONFIGURATION_CHANGE)
            {
                LOG_INF("USB configured: %u, speed: %s, granted current: %uuA",
                        (unsigned)dev.configured(), magic_enum::enum_name(dev.bus_speed()).data(),
                        dev.granted_bus_current_uA());
            }
            else
            {
                LOG_INF("USB power state: %s, granted current: %uuA",
                        magic_enum::enum_name(dev.power_state()).data(),
                        dev.granted_bus_current_uA());
            }
        });

    using namespace usb::df::config;

    // provide just enough buffer space for the configuration arrays
    monotonic_storage<usb::zephyr::udc_mac::supported_speeds(), 7> config_buffer{};

    // define configurations and start device
    for (auto speed : device().speeds())
    {
        const auto config_header = header(power::bus(200), magic_enum::enum_name(speed).data());

        auto cfg = make_config(
            config_buffer.resource(), config_header,
            usb::zephyr::usb_shell::handle().config_entry(
                speed, usb::endpoint::address(0x01), usb::endpoint::address(0x81),
                usb::endpoint::address(0x8f) // note that notification endpoint is unused here
                ));
        device().set_config_for_speed(cfg, speed);
    }
    device().open();

    while (true)
    {
        this_thread::sleep_for(zephyr::infinity);
    }
}
