#include "usb/df/config.hpp"
#include "usb/df/class/hid.hpp"
#include "hid/report_protocol.hpp"
#include <iostream>
#include <magic_enum.hpp>

using namespace usb;
using namespace usb::df;
using namespace usb::df::config;

class keyboard : public ::hid::application
{
    ::hid::protocol prot_{  };
public:
    using ::hid::application::application;

    void start(::hid::protocol new_protocol) override
    {
        prot_ = new_protocol;
    }
    void stop() override
    {
    }
    void set_report(::hid::report::type type, const std::span<const uint8_t>& data) override
    {
    }
    void get_report(::hid::report::selector select, const std::span<uint8_t>& buffer) override
    {
    }
    ::hid::protocol get_protocol() const override
    {
        return prot_;
    }
};


class some_function : public function
{
public:
    some_function() : function(0) {}

    void describe_config(const config::interface& iface,
        uint8_t if_index, df::buffer& buffer) override
    {
    }
};
some_function itf1, itf2, itf3;

static const ::hid::report_protocol rp({}, 1, 0, 0);
static keyboard kb{ rp };
static usb::df::hid::function usb_kb{ kb };

static const auto single_config = make_config(config::header(config::power::self(), "single"),
    usb::df::hid::config(usb_kb,
        usb::df::config::endpoint::interrupt(usb::endpoint::address(0x81),
            rp.max_input_size, 1))
);

auto xx = std::to_array<element>({ interface { itf1 },
        config::endpoint{standard::descriptor::endpoint::bulk(usb::endpoint::address(0x01), 64)},
    interface { itf2 } });

const auto config = make_config(config::header(config::power::bus(), "myconfig"),
    xx, 
    std::to_array<element>({ interface { itf3 } })
);

auto cdc_config(function& itf, speed s)
{
    return std::to_array<element>({ interface {itf1, 0, 3},
            {standard::descriptor::endpoint::interrupt(usb::endpoint::address(0x82), 8, 50)},
            interface {itf1, 1, 4}, {standard::descriptor::endpoint::bulk(usb::endpoint::address(0x01), s)},
            {standard::descriptor::endpoint::bulk(usb::endpoint::address(0x81), s)} });
}

auto tes = make_config(config::header(config::power::shared(250, true), "myconfig"),
    cdc_config(itf1, speed::FULL), 
    cdc_config(itf2, speed::HIGH));

const config::view testconfigview{ tes };

void config_test()
{
    config::view v{ tes };
    auto testconfiglist = make_config_list(testconfigview, single_config);
    config::view_list vl{ testconfiglist };
    for (auto c : vl)
    {
        std::cout << c.info().name() << std::endl;
    }
    auto pconf = vl[0];
    std::cout << pconf.info().name() << std::endl;

    config::view null{};
    assert(!null.info().valid());
    assert(null.interfaces().size() == 0);
    for (const auto& iface : null.interfaces())
    {
        assert(false);
    }
    assert(null.interfaces().reverse().size() == 0);
    for (const auto& iface : null.interfaces().reverse())
    {
        assert(false);
    }
    assert(null.endpoints().size() == 0);
    for (const auto& ep : null.endpoints())
    {
        assert(false);
    }
    assert(null.endpoints().reverse().size() == 0);
    for (const auto& ep : null.endpoints().reverse())
    {
        assert(false);
    }

    assert(v.info().max_power_mA() == 250);
    assert(v.info().remote_wakeup() == true);
    assert(v.info().self_powered() == true);
    std::cout << "Config name: " << (const char*)v.info().name() << std::endl;

    std::cout << "Endpoints forward: " << std::endl;
    for (const auto& ep : v.endpoints())
    {
        std::cout << std::hex << static_cast<unsigned>(ep.address())
            << " type: " << magic_enum::enum_name(ep.type())
            << ", mps: " << std::dec << ep.max_packet_size()
            << std::endl;
    }
    std::cout << "Endpoints backward: " << std::endl;
    for (const auto& ep : v.endpoints().reverse())
    {
        std::cout << std::hex << static_cast<unsigned>(ep.address()) 
            << " type: " << magic_enum::enum_name(ep.type()) 
            << ", mps: " << std::dec << ep.max_packet_size() 
            << std::endl;
    }
    std::cout << "Endpoints by index: " << std::endl;
    for (const auto& ep : v.endpoints())
    {
        auto index = v.endpoints().indexof(ep);
        assert(&v.endpoints()[index] == &ep);
    }

    std::cout << "Interfaces forward: " << std::endl;
    for (const auto& iface : v.interfaces())
    {
        std::cout << static_cast<unsigned>(iface.function_index()) << std::endl;
        for (const auto& ep : iface.endpoints())
        {
            assert(&ep.interface() == &iface);
            std::cout << std::hex << static_cast<unsigned>(ep.address())
                << " type: " << magic_enum::enum_name(ep.type())
                << ", mps: " << std::dec << ep.max_packet_size()
                << std::endl;
        }
    }
    std::cout << "Interfaces backward: " << std::endl;
    for (const auto& iface : v.interfaces().reverse())
    {
        std::cout << static_cast<unsigned>(iface.function_index()) << std::endl;
        for (const auto& ep : iface.endpoints())
        {
            assert(&ep.interface() == &iface);
            std::cout << std::hex << static_cast<unsigned>(ep.address())
                << " type: " << magic_enum::enum_name(ep.type())
                << ", mps: " << std::dec << ep.max_packet_size()
                << std::endl;
        }
    }
    std::cout << "Interfaces by index: " << std::endl;
    {
        const config::interface* iface = &(v.interfaces()[0]);
        for (unsigned i = 0; iface->valid(); i++, iface = &(v.interfaces()[i]))
        {
            std::cout << static_cast<unsigned>(iface->function_index()) << std::endl;
            for (const auto& ep : iface->endpoints())
            {
                assert(&ep.interface() == iface);
                std::cout << std::hex << static_cast<unsigned>(ep.address())
                    << " type: " << magic_enum::enum_name(ep.type())
                    << ", mps: " << std::dec << ep.max_packet_size()
                    << std::endl;
            }
        }
    }
}

#if defined(_MSC_VER) && 0
int main()
{
    config_test();
	return 0;
}
#endif
