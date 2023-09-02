#include "usb/df/class/cdc.hpp"
#include "usb/df/message.hpp"
#include "usb/class/cdc.hpp"
#include "usb/standard/descriptors.hpp"

using namespace usb::df::cdc;
using namespace usb::cdc;
using namespace usb;

standard::descriptor::interface* function::get_base_functional_descriptors(class_info cinfo,
        uint8_t if_index, df::buffer& buffer)
{
    struct desc_set
    {
        standard::descriptor::interface_association if_assoc {};
        standard::descriptor::interface iface {};
        usb::cdc::descriptor::header header {};
        usb::cdc::descriptor::union_<1> union_;

        desc_set(class_info cinfo, uint8_t first_if)
                : union_(first_if)
        {
            if_assoc.bFirstInterface = iface.bInterfaceNumber = first_if;
            if_assoc.bInterfaceCount = union_.interface_count();
            &if_assoc << cinfo;
            &iface << cinfo;
        }
    };
    auto* descs = buffer.allocate<desc_set>(cinfo, if_index);

    descs->if_assoc.iFunction = descs->iface.iInterface = to_istring(0);

    return &descs->iface;
}

void function::start(const config::interface& iface, uint8_t alt_sel)
{
    if (iface.primary() && (iface.endpoints().size() > 0))
    {
        notify_eph_ = open_ep(iface.endpoints()[0]);
    }
}

void function::stop(const config::interface& iface)
{
    if (iface.primary())
    {
        close_ep(notify_eph_);
    }
    else
    {
        close_eps(data_ephs_);
    }
}

result function::notify(const notification::header& data)
{
    return send_ep(ep_notify_handle(), std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data),
        sizeof(notification::header) + data.wLength));
}

df::config::elements<4> usb::df::cdc::config(function& fn, const config::endpoint& out_ep,
    const config::endpoint& in_ep)
{
    assert((out_ep.address().direction() == direction::OUT) and
        (in_ep.address().direction() == direction::IN));
    return config::to_elements({ config::interface(fn, 0),
        config::interface(fn, 1), out_ep, in_ep });
}

df::config::elements<5> usb::df::cdc::config(function& fn, const config::endpoint& out_ep,
    const config::endpoint& in_ep, const config::endpoint& notify_in_ep)
{
    assert((out_ep.address().direction() == direction::OUT) and 
        (in_ep.address().direction() == direction::IN) and
        (notify_in_ep.address().direction() == direction::IN));
    return config::to_elements({ config::interface(fn, 0), notify_in_ep,
        config::interface(fn, 1), out_ep, in_ep });
}

df::config::elements<5> usb::df::cdc::config(function& fn, usb::speed speed, endpoint::address out_ep_addr,
    endpoint::address in_ep_addr, endpoint::address notify_in_ep_addr, uint8_t notify_in_ep_interval)
{
    return config(fn, config::endpoint::bulk(out_ep_addr, speed), config::endpoint::bulk(in_ep_addr, speed),
        config::endpoint::interrupt(notify_in_ep_addr, sizeof(notification::header), notify_in_ep_interval));
}

df::config::elements<5> usb::df::cdc::config(function& fn, const config::endpoint& out_ep,
    const config::endpoint& in_ep, endpoint::address notify_in_ep_addr)
{
    assert((out_ep.address().direction() == direction::OUT) and
        (in_ep.address().direction() == direction::IN) and
        (notify_in_ep_addr.direction() == direction::IN));
    return config::to_elements({ config::interface(fn, 0),
        config::endpoint(config::endpoint::interrupt(notify_in_ep_addr, 8, std::numeric_limits<uint8_t>::max()), true),
        config::interface(fn, 1), out_ep, in_ep });
}

df::config::elements<5> usb::df::cdc::config(function& fn, usb::speed speed, endpoint::address out_ep_addr,
    endpoint::address in_ep_addr, endpoint::address notify_in_ep_addr)
{
    return config(fn, config::endpoint::bulk(out_ep_addr, speed), config::endpoint::bulk(in_ep_addr, speed),
        notify_in_ep_addr);
}