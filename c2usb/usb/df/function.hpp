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
#ifndef __USB_DF_FUNCTION_HPP_
#define __USB_DF_FUNCTION_HPP_

#include "usb/df/config.hpp"
#include "usb/df/transfer.hpp"
#include "usb/standard/requests.hpp"

namespace usb::df
{
class buffer;
class message;
class string_message;
class mac;

/// @brief  The function is the base class for all USB device functions.
///         It provides the abstract interface for the device class,
///         and it also provides a restricted interface to the mac for its subclasses.
class function : public polymorphic
{
  public:
    void handle_control_setup(message& msg, const config::interface& iface);
    void handle_control_setup(message& msg, ep_handle eph);
    void handle_control_data(message& msg, const config::interface& iface);

    virtual void describe_config(const config::interface& iface, uint8_t if_index,
                                 df::buffer& buffer) = 0;

    void free_string_index();
    void allocate_string_index(istring* pindex);
    bool send_owned_string(istring index, string_message& smsg);

    void init(const config::interface& iface, mac* m);
    void deinit(const config::interface& iface);

    virtual const char* ms_compatible_id() const { return nullptr; }

    virtual void transfer_complete([[maybe_unused]] ep_handle eph,
                                   [[maybe_unused]] const transfer& t)
    {}

  protected:
    ep_handle open_ep(const config::endpoint& ep);
    template <size_t T>
    void open_eps(const config::interface_endpoint_view& eps, std::array<ep_handle, T>& handles)
    {
        assert(eps.size() <= handles.size());
        auto h = handles.begin();
        for (auto& ep : eps)
        {
            *h = open_ep(ep);
            h++;
        }
    }
    void close_ep(ep_handle& eph);
    template <size_t T>
    void close_eps(std::array<ep_handle, T>& handles)
    {
        for (auto& h : handles)
        {
            close_ep(h);
        }
    }
    result send_ep(ep_handle eph, const std::span<const uint8_t>& data);
    result receive_ep(ep_handle eph, const std::span<uint8_t>& data);
    result stall_ep(ep_handle eph, bool stall);

    message* pending_message();

    virtual void control_setup_request(message& msg, const config::interface& iface);
    virtual void control_data_complete(message& msg, const config::interface& iface);
    virtual bool control_endpoint_state([[maybe_unused]] ep_handle eph,
                                        [[maybe_unused]] standard::endpoint::status new_state)
    {
        return false;
    }

    virtual void send_string(uint8_t rel_index, string_message& smsg);

    virtual uint8_t get_alt_setting([[maybe_unused]] const config::interface& iface) { return 0; }

    virtual void enable([[maybe_unused]] const config::interface& iface,
                        [[maybe_unused]] uint8_t alt_sel)
    {}
    virtual void disable([[maybe_unused]] const config::interface& iface) {}

    constexpr function(istring istr_count)
        : istr_count_(istr_count)
    {}

    istring to_istring(istring relative_index) const
    {
        return (istr_count_ == 0) ? 0 : istr_base_ + relative_index;
    }

    uint8_t describe_endpoints(const config::interface& iface, df::buffer& buffer);

  private:
    void restart(const config::interface& iface, uint8_t alt_sel)
    {
        disable(iface);
        enable(iface, alt_sel);
    }

    mac* mac_ = nullptr;
    const istring istr_count_{};
    istring istr_base_{};
#if C2USB_FUNCTION_SUSPEND
    usb::standard::interface::status std_status_{};
#endif
};

class named_function : public function
{
  protected:
    constexpr named_function(const char_t* name = {})
        : function(static_cast<istring>(name != nullptr)), name_(name)
    {}
    constexpr named_function(const char_t* name, istring istr_extra_count)
        : function(1 + istr_extra_count), name_(name)
    {}

    void send_string(uint8_t rel_index, string_message& smsg) override;

  private:
    const char_t* const name_{};
};
} // namespace usb::df

#endif // __USB_DF_FUNCTION_HPP_
