// SPDX-License-Identifier: Apache-2.0
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_config.h>

#ifdef CONFIG_NET_LOOPBACK_SIMULATE_PACKET_DROP
#include <zephyr/net/loopback.h>
#endif

#include <magic_enum.hpp>
#include <usb/df/class/cdc_ncm.hpp>
#include <usb/df/config_factory.hpp>
#include <usb/df/device.hpp>
#include <usb/df/vendor/microsoft/os_extension.hpp>
#include <usb/df/vendor/zephyr/cdc_ncm_ethernet.hpp>
#include <usb/df/vendor/zephyr/udc_mac.hpp>
#include <zephyr/thread.hpp>

LOG_MODULE_REGISTER(usb_ncm_zperf, LOG_LEVEL_INF);

using namespace zephyr;

std::array<uint8_t, CONFIG_HWINFO_DEVICE_ID_LENGTH> serial_number{};

constexpr usb::product_info product_info{CONFIG_DEMO_MANUFACTURER_ID, CONFIG_DEMO_MANUFACTURER,
                                         CONFIG_DEMO_PRODUCT_ID,      CONFIG_DEMO_PRODUCT,
                                         usb::version("1.0"),         serial_number};

auto& device()
{
    static usb::df::zephyr::udc_mac mac{DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 128};
    // support for USB-NCM on Windows pre 11 through MSOS descriptors:
    static usb::df::microsoft::descriptors msos_desc{};
    static usb::df::device_instance<mac.supported_speeds()> device{mac, product_info, msos_desc};
    return device;
}

template <std::size_t SIZE>
using ntb_array = std::array<uint32_t, SIZE>;

int main()
{
    hwinfo_get_device_id(serial_number.data(), serial_number.size());

    C2USB_USB_TRANSFER_ALIGN(static ntb_array<4096>, ntb_in){};
    C2USB_USB_TRANSFER_ALIGN(static ntb_array<4096>, ntb_out){};

    auto* ncm_function =
        usb::df::zephyr::cdc_ncm_ethernet::construct<DT_NODE_FULL_NAME(DT_NODELABEL(cdc_ncm_eth0))>(
            {ntb_in, ntb_out}
#if 1 // should work both ways
            ,
            DEVICE_DT_GET(DT_NODELABEL(cdc_ncm_eth0))
#endif
        );
    if (ncm_function == nullptr)
    {
        LOG_ERR("Failed to construct NCM function");
        return -1;
    }

    using namespace usb::df::config;
    using namespace std::chrono_literals;

    constexpr auto speeds = usb::df::zephyr::udc_mac::supported_speeds();
    static std::array<std::array<element, 7>, speeds.count()> config_store{};

    for (const auto speed : speeds)
    {
        config_store[speeds.offset(speed)] = make_config(
            header(power::bus(200), magic_enum::enum_name(speed).data()),
            ncm_function->config_entry(speed, usb::endpoint::address(0x02),
                                       usb::endpoint::address(0x82), usb::endpoint::address(0x81),
                                       usb::endpoint::interval::from_rate(speed, 8ms)));
        device().set_config_for_speed(config_store[speeds.offset(speed)], speed);
    }
    device().open();

    (void)net_config_init_app(NULL, "Initializing network");

#ifdef CONFIG_NET_LOOPBACK_SIMULATE_PACKET_DROP
    loopback_set_packet_drop_ratio(1);
#endif
#if defined(CONFIG_NET_DHCPV4) && !defined(CONFIG_NET_CONFIG_SETTINGS)
    net_dhcpv4_start(net_if_get_default());
#endif
    return 0;
}
