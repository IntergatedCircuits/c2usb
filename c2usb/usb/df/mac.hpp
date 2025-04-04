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
#ifndef __USB_DF_MAC_HPP_
#define __USB_DF_MAC_HPP_

#include "usb/df/config.hpp"
#include "usb/df/message.hpp"
#include "usb/speeds.hpp"
#include "usb/standard/descriptors.hpp"
#include "usb/standard/requests.hpp"

namespace usb::df
{
class device;
/// @brief  The mac class serves as the USB device's Media Access Controller,
///         allowing access to bus resources.
class mac : public polymorphic
{
  public:
    using lpm_support_flags =
        usb::standard::descriptor::device_capability::usb_2p0_extension::attributes;

    virtual usb::speed speed() const { return speed::FULL; }

    config::view active_config() const { return active_config_; }
    bool configured() const { return active_config().valid(); }

    void set_config(config::view config);

    /// @brief  Sets the buffer used for control transfers to the passed span.
    /// @note   The buffer must be aligned with @ref C2USB_USB_TRANSFER_ALIGN()
    /// @param  buffer: the buffer span available for control transfers
    void set_control_buffer(const std::span<uint8_t>& buffer)
    {
        ctrl_msg_.buffer_.assign(buffer.data(), buffer.size());
    }

    uint16_t control_ep_max_packet_size(usb::speed speed) const
    {
        return endpoint::packet_size_limit(endpoint::type::CONTROL, speed);
    }
    message* get_pending_message(const function* caller = nullptr);

    virtual ep_handle ep_open([[maybe_unused]] const config::endpoint& ep) { return {}; }
    virtual result ep_send([[maybe_unused]] ep_handle eph,
                           [[maybe_unused]] const std::span<const uint8_t>& data)
    {
        return result::NO_TRANSPORT;
    }
    virtual result ep_receive([[maybe_unused]] ep_handle eph,
                              [[maybe_unused]] const std::span<uint8_t>& data)
    {
        return result::NO_TRANSPORT;
    }
    virtual result ep_close([[maybe_unused]] ep_handle eph) { return result::NO_TRANSPORT; }

    virtual bool ep_is_stalled([[maybe_unused]] ep_handle eph) const { return false; }
    virtual result ep_change_stall([[maybe_unused]] ep_handle eph, [[maybe_unused]] bool stall)
    {
        return result::NO_TRANSPORT;
    }

    void init(device& dev_if, const usb::speeds& speeds);
    void deinit(device& dev_if);
    void start();
    void stop();
    bool active() const { return active_; }

    standard::device::status std_status() const { return std_status_; }

    // used as bmAttributes in USB 2p0 extension descriptor (LPM)
    virtual lpm_support_flags lpm_support() { return {}; }

    power::state power_state() const { return power_state_; }
    uint32_t granted_bus_current_uA() const;
    result remote_wakeup();

    void set_remote_wakeup(bool enabled);
    void set_power_source(usb::power::source src);

    virtual const config::endpoint& ep_address_to_config(endpoint::address addr) const;
    virtual ep_handle ep_address_to_handle(endpoint::address addr) const = 0;
    virtual ep_handle ep_config_to_handle(const config::endpoint& ep) const = 0;

  protected:
    control::request& request() { return ctrl_msg_.request_; }
    const control::request& request() const { return ctrl_msg_.request_; }

    virtual void allocate_endpoints([[maybe_unused]] config::view config = {}) {}

    auto control_stage() const { return ctrl_msg_.stage(); }
    transfer control_ep_setup();
    bool control_ep_data(direction ep_dir, const transfer& t);
    void ep_transfer_complete(endpoint::address addr, ep_handle eph, const transfer& t);
    virtual void init([[maybe_unused]] const usb::speeds& speeds) {}
    virtual void deinit() {}
    virtual bool set_attached(bool attached) { return attached; }
    virtual result signal_remote_wakeup() { return result::operation_not_supported; }

    static ep_handle create_ep_handle(uint8_t raw) { return ep_handle(raw); }

    void bus_reset();

    void set_power_state(power::state new_state);

    bool control_in_zlp(const transfer& t) const
    {
        return (request().wLength > t.size()) and
               ((t.size() % control_ep_max_packet_size(speed())) == 0);
    }

    constexpr mac() = default;

  private:
    class message_control : public message
    {
      public:
        friend class mac;
        using message::message;
    };
    message_control ctrl_msg_{};
    standard::device::status std_status_{};
    power::state power_state_{};
    bool active_{};
    config::view active_config_{};
    device* dev_if_{};
};

/// @brief  MAC specialization that uses the config index of endpoints as handles.
class index_handle_mac : public mac
{
  public:
    using mac::mac;

    ep_handle ep_address_to_handle(endpoint::address addr) const override;

  protected:
    ep_handle ep_config_to_handle(const config::endpoint& ep) const override;
};

/// @brief  MAC specialization that uses the endpoint addresses as handles.
class address_handle_mac : public mac
{
  public:
    using mac::mac;

    ep_handle ep_address_to_handle(endpoint::address addr) const override;

  protected:
    endpoint::address ep_handle_to_address(ep_handle eph) const { return endpoint::address(eph); }
    ep_handle ep_config_to_handle(const config::endpoint& ep) const override
    {
        return ep_address_to_handle(ep.address());
    }
};
} // namespace usb::df

#endif // __USB_DF_MAC_HPP_
