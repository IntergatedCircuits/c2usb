#include "usb/df/config.hpp"
#include "leds_saving_keyboard.hpp"
#include "simple_keyboard.hpp"
#include "test_framework.hpp"
#include "usb/df/class/cdc.hpp"
#include "usb/df/class/cdc_acm.hpp"
#include "usb/df/class/hid.hpp"
#include "usb/df/vendor/microsoft/xinput.hpp"

using namespace usb::df;
using namespace usb::df::config;

namespace
{
class dummy_function : public usb::df::function
{
  public:
    constexpr explicit dummy_function(usb::istring owned_strings = 1)
        : usb::df::function(owned_strings)
    {}

    void describe_config(const usb::df::config::interface&, uint8_t, usb::df::buffer&) override {}
};

constexpr auto bulk_ep(uint8_t addr, uint16_t mps = 64)
{
    return usb::standard::descriptor::endpoint::bulk(usb::endpoint::address(addr), mps);
}
} // namespace

SUITE(config_)
{
    TEST_CASE("empty config view")
    {
        const auto empty = view();

        CHECK(!empty.valid());
        CHECK(empty.info().config_size() == 0);

        CHECK(empty.interfaces().count() == 0);
        CHECK(empty.endpoints().count() == 0);
        CHECK(empty.active_endpoints().count() == 0);

        auto rev_if = empty.interfaces().reverse();
        CHECK(rev_if.size() == 0);
        CHECK(rev_if.begin() == rev_if.end());

        auto rev_ep = empty.endpoints().reverse();
        CHECK(rev_ep.size() == 0);
        CHECK(rev_ep.begin() == rev_ep.end());

        CHECK(!empty.interfaces()[0].valid());
        CHECK(!empty.endpoints().at(usb::endpoint::address(0x81)).valid());
    };

    TEST_CASE("empty config list")
    {
        const auto list = view_list();
        CHECK(list.size() == 0);
        CHECK(list.empty());
        CHECK(list[0] == view());
    };

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
    };

    TEST_CASE("power and descriptor mapping")
    {
        constexpr auto p_bus = power::bus(500, true);
        CHECK(p_bus.valid());
        CHECK(!p_bus.self_powered());
        CHECK(p_bus.remote_wakeup());
        CHECK(p_bus.max_power_mA() == 500);

        constexpr auto p_self = power::self(true);
        CHECK(p_self.self_powered());
        CHECK(p_self.remote_wakeup());
        CHECK(p_self.max_power_mA() == 0);

        constexpr auto p_shared = power::shared(250, false);
        CHECK(p_shared.self_powered());
        CHECK(!p_shared.remote_wakeup());
        CHECK(p_shared.max_power_mA() == 250);

        usb::standard::descriptor::configuration desc{};
        (&desc) << p_bus;
        CHECK(desc.max_power_mA() == 500);
        CHECK(!desc.self_powered());
        CHECK(desc.remote_wakeup());
    };

    TEST_CASE("configuration api coverage")
    {
        static dummy_function f0{};
        static dummy_function f1{};

        const auto if0 = interface(f0, 0, 1, 7);
        const auto if1 = interface(f1, 1, 0, 3);

        const auto chunk0 = to_elements({element(if0), element(endpoint(bulk_ep(0x81))),
                                         element(endpoint(bulk_ep(0x01), true))});
        const auto chunk1 = to_elements({element(if1), element(endpoint(bulk_ep(0x82)))});

        const auto joined = join_elements(chunk0, chunk1);
        CHECK(joined.size() == 5);

        constexpr auto cfg_header = header(power::shared(250, true), "cfg");
        const auto cfg = make_config(cfg_header, chunk0, chunk1);
        const auto cfg_view = view(cfg);

        CHECK(cfg_view.valid());
        CHECK(cfg_view == view(cfg));
        CHECK(cfg_view.info().config_size() == cfg.size() - 1);
        CHECK(cfg_view.info().name() == cfg_header.name());
        CHECK(cfg_view.info().max_power_mA() == 250);
        CHECK(cfg_view.info().self_powered());
        CHECK(cfg_view.info().remote_wakeup());

        CHECK(cfg_view.interfaces().count() == 2);
        CHECK(cfg_view.interfaces().count([](const interface& iface) { return iface.primary(); }) ==
              1);
        CHECK(cfg_view.interfaces().reverse().size() == cfg.size() - 1);
        CHECK(cfg_view.endpoints().count() == 3);
        CHECK(cfg_view.active_endpoints().count() == 2);

        const auto& iface0 = cfg_view.interfaces()[0];
        CHECK(iface0.valid());
        CHECK(&iface0.function() == &f0);
        CHECK(iface0.primary());
        CHECK(iface0.function_index() == 0);
        CHECK(iface0.alt_setting_count() == 2);
        CHECK(iface0.variant() == 7);

        const auto& iface1_view = cfg_view.interfaces()[1];
        CHECK(iface1_view.valid());
        CHECK(&iface1_view.function() == &f1);
        CHECK(!iface1_view.primary());
        CHECK(iface1_view.function_index() == 1);
        CHECK(iface1_view.alt_setting_count() == 1);
        CHECK(iface1_view.variant() == 3);

        CHECK(iface0.endpoints().count() == 2);
        CHECK(iface0.endpoints()[0].address() == usb::endpoint::address(0x81));
        CHECK(!iface0.endpoints()[0].unused());
        CHECK(iface0.endpoints()[1].address() == usb::endpoint::address(0x01));
        CHECK(iface0.endpoints()[1].unused());
        CHECK(&iface0.endpoints()[0].interface() == &iface0);

        CHECK(iface1_view.endpoints().count() == 1);
        CHECK(iface1_view.endpoints()[0].address() == usb::endpoint::address(0x82));
        CHECK(&iface1_view.endpoints()[0].interface() == &iface1_view);

        const auto& ep82 = cfg_view.endpoints().at(usb::endpoint::address(0x82));
        CHECK(ep82.valid());
        CHECK(ep82.address() == usb::endpoint::address(0x82));
        const auto ep82_index = cfg_view.endpoints().indexof(ep82);
        CHECK(cfg_view.endpoints()[ep82_index].address() == usb::endpoint::address(0x82));
        CHECK(!cfg_view.endpoints().at(usb::endpoint::address(0x8F)).valid());

        size_t reverse_iface_count = 0;
        for (const auto& iface : cfg_view.interfaces().reverse())
        {
            (void)iface;
            reverse_iface_count++;
        }
        CHECK(reverse_iface_count == 2);

        const auto cfg2 = make_config(header(power::bus(100, false), "cfg2"), chunk1);
        const auto cfg_list = make_config_list(cfg, cfg2);
        const auto list = view_list(cfg_list);
        CHECK(list.size() == 2);
        CHECK(!list.empty());

        const std::array<view, 3> view_arr = {view(cfg), view(cfg2), view()};
        const auto list_from_views = view_list(view_arr);
        CHECK(list_from_views.size() == 2);
    };
};
