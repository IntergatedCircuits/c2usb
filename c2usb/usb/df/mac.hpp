// SPDX-License-Identifier: MPL-2.0
#pragma once
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

    [[nodiscard]] virtual usb::speed speed() const { return speed::FULL; }

    [[nodiscard]] const config::view& active_config() const { return active_config_; }
    [[nodiscard]] bool configured() const { return active_config().valid(); }

    void set_config(config::view config)
    {
        allocate_endpoints(config);
        active_config_ = config;
    }

    /// @brief  Sets the buffer used for control transfers to the passed span.
    /// @note   The buffer must be aligned with @ref C2USB_USB_TRANSFER_ALIGN()
    /// @param  buffer: the buffer span available for control transfers
    constexpr void set_control_buffer(const std::span<uint8_t>& buffer)
    {
        ctrl_msg_.buffer_.assign(buffer.data(), buffer.size());
    }

    [[nodiscard]] virtual uint16_t control_ep_max_packet_size(usb::speed speed) const
    {
        return endpoint::packet_size_limit(endpoint::type::CONTROL, speed);
    }
    [[nodiscard]] message* get_pending_message([[maybe_unused]] const function* caller = nullptr)
    {
        assert((caller == nullptr) or
               (configured() and
                (request().recipient() == control::request::recipient::INTERFACE) and
                (&(active_config().interfaces()[request().wIndex].function()) == caller)));
        return ctrl_msg_.pending_ ? &ctrl_msg_ : nullptr;
    }

    [[nodiscard]] virtual ep_handle ep_open([[maybe_unused]] const config::endpoint& ep)
    {
        return {};
    }
    virtual result ep_send([[maybe_unused]] ep_handle eph,
                           [[maybe_unused]] const std::span<const uint8_t>& data)
    {
        return result::not_supported;
    }
    virtual result ep_receive([[maybe_unused]] ep_handle eph,
                              [[maybe_unused]] const std::span<uint8_t>& data)
    {
        return result::not_supported;
    }
    virtual result ep_cancel([[maybe_unused]] ep_handle eph) { return result::not_supported; }
    virtual result ep_close([[maybe_unused]] ep_handle& eph) { return result::not_supported; }

    [[nodiscard]] virtual bool ep_is_stalled([[maybe_unused]] ep_handle eph) const { return false; }
    virtual result ep_change_stall([[maybe_unused]] ep_handle eph, [[maybe_unused]] bool stall)
    {
        return result::not_supported;
    }

    void init(device& dev_if, const usb::speeds& speeds);
    void deinit(device& dev_if);
    void start();
    void stop();
    [[nodiscard]] bool active() const { return active_; }

    [[nodiscard]] standard::device::status std_status() const { return std_status_; }

    // used as bmAttributes in USB 2p0 extension descriptor (LPM)
    [[nodiscard]] virtual lpm_support_flags lpm_support() { return {}; }

    [[nodiscard]] power::state power_state() const { return power_state_; }
    [[nodiscard]] uint32_t granted_bus_current_uA() const;
    result remote_wakeup();

    void set_remote_wakeup(bool enabled) { std_status_.remote_wakeup = enabled; }
    void set_power_source(usb::power::source src)
    {
        std_status_.self_powered = (src == usb::power::source::BUS);
    }

    [[nodiscard]] virtual const config::endpoint&
    ep_address_to_config(endpoint::address addr) const;
    [[nodiscard]] virtual ep_handle ep_address_to_handle(endpoint::address addr) const = 0;
    [[nodiscard]] virtual ep_handle ep_config_to_handle(const config::endpoint& ep) const = 0;

  protected:
    [[nodiscard]] control::request& request() { return ctrl_msg_.request_; }
    [[nodiscard]] const control::request& request() const { return ctrl_msg_.request_; }

    virtual void allocate_endpoints([[maybe_unused]] config::view config = {}) {}

    [[nodiscard]] auto control_stage() const { return ctrl_msg_.stage(); }
    [[nodiscard]] transfer control_ep_setup();
    [[nodiscard]] bool control_ep_data(direction ep_dir, const transfer& t);
    void ep_transfer_complete(endpoint::address addr, const transfer& t) const;
    virtual void init([[maybe_unused]] const usb::speeds& speeds) {}
    virtual void deinit() {}
    virtual bool set_attached(bool attached) { return attached; }
    virtual result signal_remote_wakeup() { return result::operation_not_supported; }

    [[nodiscard]] static auto create_ep_handle(uint8_t raw) { return ep_handle(raw); }

    void bus_reset();

    void set_power_state(power::state new_state);

    [[nodiscard]] bool control_in_zlp(const transfer& t) const
    {
        return (request().wLength > t.size()) and
               ((t.size() % control_ep_max_packet_size(speed())) == 0);
    }

    [[nodiscard]] auto control_buffer()
    {
        return std::span<uint8_t>(ctrl_msg_.buffer().begin(), ctrl_msg_.buffer().end());
    }

    constexpr mac(power::state power_state = power::state::L3_OFF)
        : power_state_(power_state)
    {}

  private:
    class message_control : public message
    {
      public:
        friend class mac;
        using message::message;
    };
    message_control ctrl_msg_{};
    standard::device::status std_status_{};
    power::state power_state_;
    bool active_{};
    config::view active_config_;
    device* dev_if_{};
};

/// @brief  MAC specialization that uses the config index of endpoints as handles.
class index_handle_mac : public mac
{
  public:
    using mac::mac;

    [[nodiscard]] ep_handle ep_address_to_handle(endpoint::address addr) const override;

  protected:
    [[nodiscard]] ep_handle ep_config_to_handle(const config::endpoint& ep) const override;
};

/// @brief  MAC specialization that uses the endpoint addresses as handles.
class address_handle_mac : public mac
{
  public:
    using mac::mac;

    [[nodiscard]] ep_handle ep_address_to_handle(endpoint::address addr) const override;

  protected:
    [[nodiscard]] static endpoint::address ep_handle_to_address(ep_handle eph)
    {
        return endpoint::address(eph);
    }
    [[nodiscard]] ep_handle ep_config_to_handle(const config::endpoint& ep) const override
    {
        return ep_address_to_handle(ep.address());
    }
};
} // namespace usb::df
