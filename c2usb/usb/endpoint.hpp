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
#ifndef __USB_ENDPOINT_HPP_
#define __USB_ENDPOINT_HPP_

#include "usb/base.hpp"

namespace usb::endpoint
{
enum class type : uint8_t
{
    CONTROL = 0,
    ISOCHRONOUS = 1,
    BULK = 2,
    INTERRUPT = 3
};

namespace isochronous
{
enum class sync : uint8_t
{
    NONE = 0,
    ASYNCHRONOUS = 1,
    ADAPTIVE = 2,
    SYNCHRONOUS = 3
};

enum class usage : uint8_t
{
    DATA = 0,
    FEEDBACK = 1,
    EXPLICIT_FEEDBACK_DATA = 2,
};
} // namespace isochronous

C2USB_STATIC_CONSTEXPR inline uint16_t packet_size_limit(type t, speed s)
{
    if (s == speed::NONE)
    {
        return 0;
    }
    C2USB_STATIC_CONSTEXPR static const uint16_t sizes[4][3] = {
        // LS    FS    HS
        {8, 64, 64},     // CONTROL
        {0, 1023, 1024}, // ISOCHRONOUS
        {0, 64, 512},    // BULK
        {8, 64, 1024}    // INTERRUPT
    };
    return sizes[static_cast<uint8_t>(t)]
                [static_cast<uint8_t>(s) - static_cast<uint8_t>(speed::LOW)];
}

class address
{
  public:
    constexpr operator uint8_t() const { return value_; }
    constexpr operator uint8_t&() { return value_; }

    constexpr explicit address(uint8_t value)
        : value_(value /* & 0x8F */)
    {}
    constexpr address(usb::direction dir, uint8_t number)
        : value_((static_cast<uint8_t>(dir) << 7) | (number & 0xF))
    {}
    constexpr uint8_t number() const { return value_ & 0xF; }

    constexpr bool valid() const { return (value_ & 0x70) == 0; }
    constexpr static address invalid() { return address(0x70); }
    constexpr static address control(usb::direction dir) { return address(dir, 0); }
    constexpr static address control_in() { return control(usb::direction::IN); }
    constexpr static address control_out() { return control(usb::direction::OUT); }

    constexpr usb::direction direction() const { return static_cast<usb::direction>(value_ >> 7); }

  private:
    uint8_t value_;
};
} // namespace usb::endpoint

#endif // __USB_ENDPOINT_HPP_
