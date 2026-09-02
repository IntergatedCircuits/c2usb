// SPDX-License-Identifier: MPL-2.0
#pragma once

#include "hid/transport.hpp"
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
    constexpr app_base_function(::hid::application& app, const char_t* name = {},
                                istring istr_extra_count = 0)
        : df::named_function(name, istr_extra_count), app_(app)
    {}

    [[nodiscard]] constexpr const ::hid::application& app() const { return app_; }

  protected:
    void start(const config::interface& iface, ::hid::boot::mode prot);
    void disable(const config::interface& iface) override;

    c2usb::result send_report(::hid::session& sess, const std::span<const uint8_t>& data) override;
    c2usb::result receive_report(::hid::session& sess, const std::span<uint8_t>& data,
                                 ::hid::report::type type = ::hid::report::type::OUTPUT) override;

    void ep_callback(const transfer& t) override;

    ep_handle& ep_in_handle() { return ephs_[0]; }
    ep_handle& ep_out_handle() { return ephs_[1]; }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    ::hid::application& app_;
    ::hid::session* session_{};
    reports_receiver rx_buffers_;
    std::array<ep_handle, 2> ephs_{};
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

/// @brief  The function is the actual USB HID function, implementing the full functionality.
class function : public app_base_function
{
  public:
    // IN endpoint only
    [[nodiscard]] df::config::elements<2> config_entry(const df::config::endpoint& in_ep)
    {
        assert(in_ep.address().direction() == direction::IN);
        return config::to_elements({df::config::interface{*this}, in_ep});
    }
    [[nodiscard]] df::config::elements<2>
    config_entry(usb::speed speed, endpoint::address in_ep_addr, uint8_t in_interval)
    {
        const size_t in_mps =
            std::min(this->app().report_info().max_input_size,
                     endpoint::packet_size_limit(endpoint::type::INTERRUPT, speed));
        return config_entry(config::endpoint::interrupt(in_ep_addr, in_mps, in_interval));
    }

    // IN and OUT endpoints
    [[nodiscard]] df::config::elements<3> config_entry(const df::config::endpoint& in_ep,
                                                       const df::config::endpoint& out_ep)
    {
        assert((in_ep.address().direction() == direction::IN) and
               (out_ep.address().direction() == direction::OUT));
        return config::to_elements({df::config::interface{*this}, in_ep, out_ep});
    }
    [[nodiscard]] df::config::elements<3>
    config_entry(usb::speed speed, endpoint::address in_ep_addr, uint8_t in_interval,
                 endpoint::address out_ep_addr, uint8_t out_interval)
    {
        const size_t in_mps =
            std::min(this->app().report_info().max_input_size,
                     endpoint::packet_size_limit(endpoint::type::INTERRUPT, speed));
        const size_t out_mps =
            std::min(this->app().report_info().max_output_size,
                     endpoint::packet_size_limit(endpoint::type::INTERRUPT, speed));
        return config_entry(config::endpoint::interrupt(in_ep_addr, in_mps, in_interval),
                            config::endpoint::interrupt(out_ep_addr, out_mps, out_interval));
    }

#ifndef CONFIG_C2USB_HID_BOOT_PROTOCOL
    constexpr function(::hid::application& app, const char_t* name = {})
        : app_base_function(app, name)
    {}
#else
    constexpr function(::hid::application& app, boot_protocol_mode mode = boot_protocol_mode::NONE,
                       usb::hid::country_code country = usb::hid::country_code::NOT_SUPPORTED)
        : app_base_function(app), protocol_mode_(mode), country_code_(country)
    {}
    constexpr function(::hid::application& app, const char_t* name,
                       boot_protocol_mode mode = boot_protocol_mode::NONE,
                       usb::hid::country_code country = usb::hid::country_code::NOT_SUPPORTED)
        : app_base_function(app, name), protocol_mode_(mode), country_code_(country)
    {}
#endif

  protected:
#ifndef CONFIG_C2USB_HID_BOOT_PROTOCOL
    constexpr function(::hid::application& app, const char_t* name, istring istr_extra_count)
        : app_base_function(app, name, istr_extra_count)
    {}
#else
    constexpr function(::hid::application& app, const char_t* name, istring istr_extra_count,
                       boot_protocol_mode mode,
                       usb::hid::country_code country = usb::hid::country_code::NOT_SUPPORTED)
        : app_base_function(app, name, istr_extra_count),
          protocol_mode_(mode),
          country_code_(country)
    {}
#endif

  private:
    void get_hid_descriptor(df::buffer& buffer) const;
    void get_descriptor(message& msg) const;

    void describe_config(const config::interface& iface, uint8_t if_index,
                         df::buffer& buffer) const override;

    void control_setup_request(message& msg, const config::interface& iface) override;
    void control_data_complete(message& msg, const config::interface& iface) override;

    void set_protocol(message& msg, const config::interface& iface);

#if CONFIG_C2USB_HID_BOOT_PROTOCOL
    [[nodiscard]] boot_protocol_mode protocol_mode() const { return protocol_mode_; }
    [[nodiscard]] usb::hid::country_code country_code() const { return country_code_; }
    const boot_protocol_mode protocol_mode_;
    const usb::hid::country_code country_code_;
#else
   [[nodiscard]] boot_protocol_mode protocol_mode() const { return boot_protocol_mode::NONE; }
   [[nodiscard]] usb::hid::country_code country_code() const { return usb::hid::country_code::NOT_SUPPORTED; }
#endif
};

/// @brief  The string_function extends baseline HID functionality with fixed string descriptors.
///         HID class functions can specify string descriptors in the HID report descriptor,
///         as well as in the report data.
class string_function : public function
{
  public:
#ifndef CONFIG_C2USB_HID_BOOT_PROTOCOL
    constexpr string_function(::hid::application& app, const char_t* name = {},
                              reference_array_view<const char_t> extra_strings = {})
        : function(app, name, extra_strings.size()), extra_strings_(extra_strings)
    {}
#else
    constexpr string_function(
        ::hid::application& app, const char_t* name,
        reference_array_view<const char_t> extra_strings = {},
        boot_protocol_mode mode = boot_protocol_mode::NONE,
        usb::hid::country_code country = usb::hid::country_code::NOT_SUPPORTED)
        : function(app, name, extra_strings.size(), mode, country), extra_strings_(extra_strings)
    {}
#endif

    [[nodiscard]] istring string_index(istring relative_index) const
    {
        return to_istring(int(is_named()) + relative_index);
    }
#if 0
    [[nodiscard]] istring string_index(const char_t* str) const
    {
        if (str == nullptr)
        {
            return 0;
        }
        auto it = std::ranges::find(extra_strings_, str);
        if (it == extra_strings_.end())
        {
            return 0;
        }
        return string_index(std::distance(extra_strings_.begin(), it));
    }
#endif

  private:
    reference_array_view<const char_t> extra_strings_;

    void send_string(uint8_t rel_index, string_message& smsg) override;
};

} // namespace usb::df::hid
