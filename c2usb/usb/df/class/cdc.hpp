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
  public:
    result notify(const usb::cdc::notification::header& data);

  protected:
    using df::named_function::named_function;

    standard::descriptor::interface*
    get_base_functional_descriptors(class_info cinfo, uint8_t if_index, df::buffer& buffer);

    void start(const config::interface& iface, uint8_t alt_sel) override;
    void stop(const config::interface& iface) override;

    std::array<ep_handle, 2> data_ephs_{};
    ep_handle& ep_out_handle() { return data_ephs_[0]; }
    ep_handle& ep_in_handle() { return data_ephs_[1]; }
    ep_handle notify_eph_{};
    ep_handle& ep_notify_handle() { return notify_eph_; }
};

// no notification endpoint
config::elements<4> config(function& fn, const config::endpoint& out_ep,
                           const config::endpoint& in_ep);

// active notification endpoint
config::elements<5> config(function& fn, const config::endpoint& out_ep,
                           const config::endpoint& in_ep, const config::endpoint& notify_in_ep);

config::elements<5> config(function& fn, usb::speed speed, endpoint::address out_ep_addr,
                           endpoint::address in_ep_addr, endpoint::address notify_in_ep_addr,
                           uint8_t notify_in_ep_interval);

// unused notification endpoint
config::elements<5> config(function& fn, const config::endpoint& out_ep,
                           const config::endpoint& in_ep, endpoint::address notify_in_ep_addr);
config::elements<5> config(function& fn, usb::speed speed, endpoint::address out_ep_addr,
                           endpoint::address in_ep_addr, endpoint::address notify_in_ep_addr);
} // namespace usb::df::cdc

#endif // __USB_DF_CLASS_CDC_HPP_
