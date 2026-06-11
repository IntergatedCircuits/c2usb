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
#ifndef __USB_DF_CLASS_HID_HPP_
#define __USB_DF_CLASS_HID_HPP_

#include "hid/application.hpp"
#include "usb/class/hid.hpp"
#include "usb/df/function.hpp"

namespace usb::df::hid
{
using namespace usb::hid;

/// @brief  This is a partial implementation of the HID function, that interfaces to the application
///         without any USB HID protocol specifics, so it can be reused in other HID-like functions.
class app_base_function : public df::named_function, public ::hid::transport
{
  public:
    constexpr app_base_function(::hid::application& app, const char_t* name = {})
        : df::named_function(name), app_(app)
    {}

    constexpr const ::hid::application& app() const { return app_; }

  protected:
    void start(const config::interface& iface, ::hid::protocol prot);
    void disable(const config::interface& iface) override;

    result send_report(const std::span<const uint8_t>& data, ::hid::report::type type) override;
    result receive_report(const std::span<uint8_t>& data, ::hid::report::type type) override;
    ::hid::transport::type transport_type() const override { return ::hid::transport::type::USB; }

    void transfer_complete(ep_handle eph, const transfer& t) override;

    ::hid::application& app_;
    ::hid::reports_receiver rx_buffers_{};
    ::hid::report::selector get_report_{};
    std::array<ep_handle, 2> ephs_{};
    ep_handle& ep_in_handle() { return ephs_[0]; }
    ep_handle& ep_out_handle() { return ephs_[1]; }
};

/// @brief  The function is the actual USB HID function, implementing the full functionality.
class function : public app_base_function
{
  public:
    constexpr function(::hid::application& app, boot_protocol_mode mode = boot_protocol_mode::NONE)
        : app_base_function(app), protocol_(mode)
    {}
    constexpr function(::hid::application& app, const char_t* name,
                       boot_protocol_mode mode = boot_protocol_mode::NONE)
        : app_base_function(app, name), protocol_(mode)
    {}

  protected:
    virtual void get_hid_descriptor(df::buffer& buffer);
    virtual void get_descriptor(message& msg);

    void describe_config(const config::interface& iface, uint8_t if_index,
                         df::buffer& buffer) override;

    void control_setup_request(message& msg, const config::interface& iface) override;
    void control_data_complete(message& msg, const config::interface& iface) override;

    boot_protocol_mode protocol_mode() const { return protocol_; }
    const boot_protocol_mode protocol_;
};

inline df::config::elements<2> config(function& fn, const df::config::endpoint& in_ep)
{
    assert(in_ep.address().direction() == direction::IN);
    return config::to_elements({df::config::interface{fn}, in_ep});
}

inline df::config::elements<2> config(function& fn, usb::speed speed, endpoint::address in_ep_addr,
                                      uint8_t in_interval)
{
    const size_t in_mps = std::min(fn.app().report_info().max_input_size,
                                   endpoint::packet_size_limit(endpoint::type::INTERRUPT, speed));
    return config(fn, config::endpoint::interrupt(in_ep_addr, in_mps, in_interval));
}

inline df::config::elements<3> config(function& fn, const df::config::endpoint& in_ep,
                                      const df::config::endpoint& out_ep)
{
    assert((in_ep.address().direction() == direction::IN) and
           (out_ep.address().direction() == direction::OUT));
    return config::to_elements({df::config::interface{fn}, in_ep, out_ep});
}

inline df::config::elements<3> config(function& fn, usb::speed speed, endpoint::address in_ep_addr,
                                      uint8_t in_interval, endpoint::address out_ep_addr,
                                      uint8_t out_interval)
{
    const size_t in_mps = std::min(fn.app().report_info().max_input_size,
                                   endpoint::packet_size_limit(endpoint::type::INTERRUPT, speed));
    const size_t out_mps = std::min(fn.app().report_info().max_output_size,
                                    endpoint::packet_size_limit(endpoint::type::INTERRUPT, speed));
    return config(fn, config::endpoint::interrupt(in_ep_addr, in_mps, in_interval),
                  config::endpoint::interrupt(out_ep_addr, out_mps, out_interval));
}

} // namespace usb::df::hid

#endif // __USB_DF_CLASS_HID_HPP_
