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
#ifndef __USB_CONTROL_HPP_
#define __USB_CONTROL_HPP_

#include "usb/base.hpp"

namespace usb::control
{
    enum class stage : uint8_t
    {
        RESET   = 0,
        SETUP   = 1,
        DATA    = 2,
        STATUS  = 3,
    };

    struct request_base
    {
        enum class recipient : uint8_t
        {
            DEVICE      = 0, /// The request is addressed to the device
            INTERFACE   = 1, /// The request is addressed to an interface
            ENDPOINT    = 2, /// The request is addressed to an endpoint
            OTHER       = 3, /// The request is custom addressed
        };

        enum class type : uint8_t
        {
            STANDARD    = 0, /// The request is specified by USB 2.0 standard
            CLASS       = 1, /// The request is specified by a standard USB class
            VENDOR      = 2, /// The request is specified by product vendor
        };

        constexpr request_base() = default;
        constexpr bool operator==(const request_base&) const = default;
    };

    /// @brief  The request_id class is a unique identifier for control requests,
    ///         by combining the type and code bytes.
    struct request_id : public request_base
    {
        uint8_t bmRequestType {};
        uint8_t bRequest {};

        constexpr request_id()
        {}
        template <typename T>
        constexpr request_id(usb::direction dir, request_base::type t, request_base::recipient rec, T code)
                : bmRequestType((static_cast<uint8_t>(dir) << 7) |
                        (static_cast<uint8_t>(t) << 5) |
                        (static_cast<uint8_t>(rec))),
                  bRequest(static_cast<uint8_t>(code))
        {}

        constexpr auto direction() const { return static_cast<usb::direction>(bmRequestType >> 7); }
        constexpr auto type() const      { return static_cast<request_base::type>((bmRequestType >> 5) & 3); }
        constexpr auto recipient() const { return static_cast<request_base::recipient>(bmRequestType & 0x1F); }
        constexpr auto code() const      { return bRequest; }

        constexpr bool operator==(const request_id&) const = default;

        constexpr operator uint16_t() const
        {
#ifdef __cpp_lib_bit_cast
            return std::bit_cast<uint16_t>(*this);
#else
            return (bmRequestType << 8) | bRequest;
#endif
        }

        uint8_t* data()             { return &bmRequestType; }
        const uint8_t* data() const { return &bmRequestType; }
    };

    /// @brief  The request class stores the USB device request contents
    ///         of the Setup packet over the control pipe.
    struct request : public request_id
    {
        using request_id::request_id;

        template <typename T>
        constexpr request(usb::direction dir, request_base::type t, request_base::recipient rec,
            T code, uint16_t value, uint16_t len = 0, uint16_t index = 0)
            : request_id(dir, t, rec, code), wValue(value), wIndex(index), wLength(len)
        {}

        struct splittable_uint16_t : public le_uint16_t
        {
            constexpr uint8_t high_byte() const { return this->storage[1]; }
            constexpr uint8_t &high_byte()      { return this->storage[1]; }
            constexpr uint8_t low_byte() const  { return this->storage[0]; }
            constexpr uint8_t &low_byte()       { return this->storage[0]; }

            template <typename T>
            constexpr bool operator==(const T& rhs) const
            {
                return static_cast<uint16_t>(*this) == static_cast<uint16_t>(rhs);
            }
        };

        splittable_uint16_t wValue;
        splittable_uint16_t wIndex;
        splittable_uint16_t wLength;

        constexpr auto id() const           { return static_cast<request_id>(*this); }
        constexpr static size_t size()      { return sizeof(request); }
    };
    static_assert(sizeof(request) == 8);
}

#endif // __USB_CONTROL_HPP_
