// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "usb/df/class/hid.hpp"
#include "usb/vendor/microsoft_xusb.hpp"

namespace usb::df::microsoft
{
/// @brief  The xfunction implements the XBOX360 controller USB interface.
class xfunction : public df::hid::app_base_function
{
  public:
    [[nodiscard]] df::config::elements<3> config_entry(const df::config::endpoint& in_ep,
                                                       const df::config::endpoint& out_ep)
    {
        assert((in_ep.address().direction() == direction::IN) and
               (out_ep.address().direction() == direction::OUT));
        return config::to_elements({df::config::interface{*this}, in_ep, out_ep});
    }

    [[nodiscard]] df::config::elements<3> config_entry(endpoint::address in_addr,
                                                       uint8_t in_interval,
                                                       endpoint::address out_addr,
                                                       uint8_t out_interval)
    {
        return config_entry(
            standard::descriptor::endpoint::interrupt(
                in_addr, usb::microsoft::xusb::MAX_INPUT_REPORT_SIZE, in_interval),
            standard::descriptor::endpoint::interrupt(
                out_addr, usb::microsoft::xusb::MAX_OUTPUT_REPORT_SIZE, out_interval));
    }

    xfunction(::hid::application& app, const char_t* name = {})
        : app_base_function(app, name)
    {}

    /// @brief  Use a custom non-HID protocol code, as this report layout cannot be made compatible
    ///         with HID report protocol (due to using report ID 0)
    constexpr static auto PROTOCOL = static_cast<::hid::boot::mode>('X');

  private:
    void describe_config(const config::interface& iface, uint8_t if_index,
                         df::buffer& buffer) const override;

    [[nodiscard]] std::string_view ms_compatible_id() const override
    {
        return usb::microsoft::xusb::COMPATIBLE_ID;
    }

    void enable(const config::interface& iface, uint8_t alt_sel) override;
};

} // namespace usb::df::microsoft
