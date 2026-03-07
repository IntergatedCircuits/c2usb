/// @file
///
/// @author Benedek Kupper
/// @date   2023
///
/// @copyright
///         This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
///         If a copy of the MPL was not distributed with this file, You can obtain one at
///         https://mozilla.org/MPL/2.0/.
///
#ifndef __USB_DF_CLASS_CDC_HPP_
#define __USB_DF_CLASS_CDC_HPP_

#include "usb/class/cdc.hpp"
#include "usb/df/function.hpp"

namespace usb::standard::descriptor
{
struct interface;
}

namespace usb::df::cdc
{
class function : public df::named_function
{
  protected:
    using df::named_function::named_function;

    standard::descriptor::interface*
    get_base_functional_descriptors(class_info cinfo, uint8_t if_index, df::buffer& buffer);

    void open_notify_ep(const config::interface& iface)
    {
        assert(iface.primary());
        if (iface.endpoints().size() > 0)
        {
            notify_eph_ = open_ep(iface.endpoints()[0]);
        }
    }

    void open_data_eps(const config::interface& iface)
    {
        assert(!iface.primary());
        assert(iface.endpoints().size() == 2);
        open_eps(iface.endpoints(), data_ephs_);
    }

    result notify(const usb::cdc::notification::header& data)
    {
        return send_ep(
            ep_notify_handle(),
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data),
                                     sizeof(usb::cdc::notification::header) + data.wLength));
    }
    result send_data(const std::span<const uint8_t>& data) { return send_ep(ep_in_handle(), data); }
    result receive_data(const std::span<uint8_t>& data)
    {
        return receive_ep(ep_out_handle(), data);
    }

    ep_handle ep_out_handle() const { return data_ephs_[0]; }
    ep_handle ep_in_handle() const { return data_ephs_[1]; }
    ep_handle ep_notify_handle() const { return notify_eph_; }

    void disable(const config::interface& iface) override
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

  private:
    std::array<ep_handle, 2> data_ephs_{};
    ep_handle notify_eph_{};
};

// no notification endpoint
inline df::config::elements<4> config(function& fn, const config::endpoint& out_ep,
                                      const config::endpoint& in_ep)
{
    assert((out_ep.address().direction() == direction::OUT) and
           (in_ep.address().direction() == direction::IN));
    return config::to_elements({config::interface(fn, 0), config::interface(fn, 1), out_ep, in_ep});
}

// active notification endpoint
inline df::config::elements<5> config(function& fn, const config::endpoint& out_ep,
                                      const config::endpoint& in_ep,
                                      const config::endpoint& notify_in_ep)
{
    assert((out_ep.address().direction() == direction::OUT) and
           (in_ep.address().direction() == direction::IN) and
           (notify_in_ep.address().direction() == direction::IN));
    return config::to_elements(
        {config::interface(fn, 0), notify_in_ep, config::interface(fn, 1), out_ep, in_ep});
}

inline df::config::elements<5> config(function& fn, usb::speed speed, endpoint::address out_ep_addr,
                                      endpoint::address in_ep_addr,
                                      endpoint::address notify_in_ep_addr,
                                      uint8_t notify_in_ep_interval)
{
    return config(
        fn, config::endpoint::bulk(out_ep_addr, speed), config::endpoint::bulk(in_ep_addr, speed),
        config::endpoint::interrupt(notify_in_ep_addr, sizeof(usb::cdc::notification::header),
                                    notify_in_ep_interval));
}

// unused notification endpoint
inline df::config::elements<5> config(function& fn, const config::endpoint& out_ep,
                                      const config::endpoint& in_ep,
                                      endpoint::address notify_in_ep_addr)
{
    assert((out_ep.address().direction() == direction::OUT) and
           (in_ep.address().direction() == direction::IN) and
           (notify_in_ep_addr.direction() == direction::IN));
    return config::to_elements(
        {config::interface(fn, 0),
         config::endpoint(
             config::endpoint::interrupt(notify_in_ep_addr, 8, std::numeric_limits<uint8_t>::max()),
             true),
         config::interface(fn, 1), out_ep, in_ep});
}

inline df::config::elements<5> config(function& fn, usb::speed speed, endpoint::address out_ep_addr,
                                      endpoint::address in_ep_addr,
                                      endpoint::address notify_in_ep_addr)
{
    return config(fn, config::endpoint::bulk(out_ep_addr, speed),
                  config::endpoint::bulk(in_ep_addr, speed), notify_in_ep_addr);
}

} // namespace usb::df::cdc

#endif // __USB_DF_CLASS_CDC_HPP_
