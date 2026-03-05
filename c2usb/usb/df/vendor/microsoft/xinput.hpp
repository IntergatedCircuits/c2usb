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
#ifndef __USB_DF_MICROSOFT_XINPUT_HPP_
#define __USB_DF_MICROSOFT_XINPUT_HPP_

#include "usb/df/class/hid.hpp"
#include "usb/vendor/microsoft_xusb.hpp"

namespace usb::df::microsoft
{
/// @brief  The xfunction implements the XBOX360 controller USB interface.
class xfunction : public df::hid::app_base_function
{
  public:
    xfunction(::hid::application& app, const char_t* name = {})
        : app_base_function(app, name)
    {}

    /// @brief  Use a custom non-HID protocol code, as this report layout cannot be made compatible
    /// with
    ///         HID report protocol (due to using report ID 0)
    constexpr static inline auto PROTOCOL = static_cast<::hid::protocol>('X');

  private:
    void describe_config(const config::interface& iface, uint8_t if_index,
                         df::buffer& buffer) override;

    const char* ms_compatible_id() const override { return usb::microsoft::xusb::COMPATIBLE_ID; }

    void enable(const config::interface& iface, uint8_t alt_sel) override;
};

config::elements<3> xconfig(xfunction& fn, const df::config::endpoint& in_ep,
                            const df::config::endpoint& out_ep);

config::elements<3> xconfig(xfunction& fn, endpoint::address in_addr, uint8_t in_interval,
                            endpoint::address out_addr, uint8_t out_interval);
} // namespace usb::df::microsoft

#endif // __USB_DF_MICROSOFT_XINPUT_HPP_
