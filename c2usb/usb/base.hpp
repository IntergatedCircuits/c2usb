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
#ifndef __USB_BASE_HPP_
#define __USB_BASE_HPP_

#include "c2usb.hpp"

namespace usb
{
using namespace c2usb;

using char_t = char;
using istring = uint8_t;

enum class direction : uint8_t
{
    OUT = 0, /// Host to device
    IN = 1,  /// Device to host
};

constexpr inline direction opposite_direction(direction dir)
{
    return static_cast<direction>(1 - static_cast<uint8_t>(dir));
}

/// @note Modifications of this enum must be reflected in @ref usb::endpoint::packet_size_limit()
enum class speed : uint8_t
{
    NONE = 0, /// Invalid
    LOW = 1,  /// Low speed:  1.5 MBaud
    FULL = 2, /// Full speed:  12 MBaud
    HIGH = 3, /// High speed: 480 MBaud
};
constexpr auto operator<=>(speed a, speed b)
{
    return static_cast<uint8_t>(a) <=> static_cast<uint8_t>(b);
}

struct descriptor_header
{
    uint8_t bLength{};
    uint8_t bDescriptorType{};

    constexpr descriptor_header(uint8_t desc_size, uint8_t desc_type)
        : bLength(desc_size), bDescriptorType(desc_type)
    {}

    operator std::span<uint8_t>() { return {&bLength, bLength}; }
    operator std::span<const uint8_t>() const { return {&bLength, bLength}; }
};

template <class T>
struct descriptor : public descriptor_header
{
    constexpr static uint8_t size() { return sizeof(T); }
    constexpr static uint8_t type() { return static_cast<uint8_t>(T::TYPE_CODE); }
    constexpr descriptor()
        : descriptor_header(size(), type())
    {}
    constexpr descriptor(uint8_t length)
        : descriptor_header(length, type())
    {}
};

// TODO: https://github.com/mariusbancila/stduuid
struct uuid : public std::array<uint8_t, 16>
{};

namespace power
{
/// @brief USB power source selection
enum class source : uint8_t
{
    BUS = 0,    /// Device is USB bus powered
    DEVICE = 1, /// Device is independently powered
};

/// @brief USB power source selection
enum class state : uint8_t
{
    L3_OFF = 0, /// Powered off
    L2_SUSPEND, /// Suspended bus
    L1_SLEEP,   /// Faster transitioning low power mode
    L0_ON       /// Active or idle bus, SOF present
};
} // namespace power
} // namespace usb

#endif // __USB_BASE_HPP_
