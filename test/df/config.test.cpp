#include "usb/df/config.hpp"
#include "high_resolution_mouse.hpp"
#include "leds_saving_keyboard.hpp"
#include "simple_keyboard.hpp"
#include "usb/df/class/cdc.hpp"
#include "usb/df/class/cdc_acm.hpp"
#include "usb/df/class/hid.hpp"
#include "usb/df/vendor/microsoft_xinput.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace usb::df;
using namespace usb::df::config;

TEST_CASE("empty config list")
{
    const auto list = view_list();
    CHECK(list.size() == 0);
    CHECK(list.empty());
    CHECK(list[0] == view());
}

TEST_CASE("full config")
{
    static constexpr auto speed = usb::speed::FULL;
    static cdc::acm::function serial{};
    static leds_saving_keyboard kb_handle{::hid::page::keyboard_keypad::KEYBOARD_CAPS_LOCK,
                                          ::hid::page::leds::CAPS_LOCK};
    static usb::df::hid::function hid_kb{kb_handle, usb::hid::boot_protocol_mode::KEYBOARD};
    static microsoft::xfunction xpad_kb{kb_handle};

    constexpr auto config_header = header(power::bus(500, true));
    const auto shared_config_elems =
        join_elements(cdc::config(serial, speed, usb::endpoint::address(0x01),
                                  usb::endpoint::address(0x81), usb::endpoint::address(0x8f)));

    static const auto hid_config =
        make_config(config_header, shared_config_elems,
                    usb::df::hid::config(hid_kb, speed, usb::endpoint::address(0x82), 1));

    static const auto xpad_config =
        make_config(config_header, shared_config_elems,
                    microsoft::xconfig(xpad_kb, usb::endpoint::address(0x83), 1,
                                       usb::endpoint::address(0x03), 1));

    static const auto configs = make_config_list(hid_config, xpad_config);
    const auto list = view_list(configs);
    CHECK(list.size() == 2);
    CHECK(!list.empty());

    auto view = list[0];
    CHECK(view.valid());
    CHECK(view.info().config_size() == hid_config.size() - 1);
    CHECK(view.interfaces().reverse().size() == hid_config.size() - 1);
    CHECK(view.info().max_power_mA() == config_header.max_power_mA());
    CHECK(view.info().self_powered() == config_header.self_powered());
    CHECK(view.info().remote_wakeup() == config_header.remote_wakeup());

    CHECK(view.interfaces().count() == 3);
    CHECK(view.endpoints().count() == 4);
    CHECK(view.active_endpoints().count() == 3);

    CHECK(&view.interfaces()[0].function() == &serial);
    CHECK(view.interfaces()[0].endpoints().count() == 1);
    CHECK(view.interfaces()[0].endpoints()[0].address() == 0x8f);
    // CHECK(!view.interfaces()[0].endpoints()[1].valid());

    CHECK(&view.interfaces()[1].function() == &serial);
    CHECK(view.interfaces()[1].endpoints().count() == 2);
    CHECK(view.interfaces()[1].endpoints()[0].address() == 0x01);
    CHECK(view.interfaces()[1].endpoints()[1].address() == 0x81);
    // CHECK(!view.interfaces()[1].endpoints()[2].valid());

    CHECK(&view.interfaces()[2].function() == &hid_kb);
    CHECK(view.interfaces()[2].endpoints().count() == 1);
    CHECK(view.interfaces()[2].endpoints()[0].address() == 0x82);
    // CHECK(!view.interfaces()[2].endpoints()[1].valid());

    CHECK(!view.interfaces()[3].valid());

    view = list[1];
    CHECK(view.valid());
    CHECK(view.info().config_size() == xpad_config.size() - 1);
    CHECK(view.interfaces().reverse().size() == xpad_config.size() - 1);

    CHECK(view.interfaces().count() == 3);
    CHECK(view.endpoints().count() == 5);
    CHECK(view.active_endpoints().count() == 4);

    CHECK(&view.interfaces()[0].function() == &serial);
    CHECK(view.interfaces()[0].endpoints().count() == 1);
    CHECK(view.interfaces()[0].endpoints()[0].address() == 0x8f);
    // CHECK(!view.interfaces()[0].endpoints()[1].valid());

    CHECK(&view.interfaces()[1].function() == &serial);
    CHECK(view.interfaces()[1].endpoints().count() == 2);
    CHECK(view.interfaces()[1].endpoints()[0].address() == 0x01);
    CHECK(view.interfaces()[1].endpoints()[1].address() == 0x81);
    // CHECK(!view.interfaces()[1].endpoints()[2].valid());

    CHECK(&view.interfaces()[2].function() == &xpad_kb);
    CHECK(view.interfaces()[2].endpoints().count() == 2);
    CHECK(view.interfaces()[2].endpoints()[0].address() == 0x83);
    CHECK(view.interfaces()[2].endpoints()[1].address() == 0x03);
    // CHECK(!view.interfaces()[2].endpoints()[2].valid());

    CHECK(!view.interfaces()[3].valid());

    view = list[2];
    CHECK(!view.valid());
    CHECK(!view.info().config_size());
}
