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
#ifndef __USB_CLASS_HID_HPP_
#define __USB_CLASS_HID_HPP_

#include "usb/standard/requests.hpp"
#include "usb/version.hpp"

namespace usb::hid
{
constexpr uint8_t CLASS_CODE = 0x03;
constexpr version SPEC_VERSION{"1.11"};

enum class boot_protocol_mode : uint8_t
{
    NONE = 0,
    KEYBOARD = 1,
    MOUSE = 2,
};

enum class request : uint8_t
{
    SET_REPORT = 0x09, /// Send a report to the device, setting the state of input, output,
                       /// or feature controls.
                       /// wValue.upper = @ref report::type wValue.lower = report::id
    GET_REPORT = 0x01, /// Receive a report via the Control pipe.
                       /// wValue.upper = @ref report::type wValue.lower = report::id
    SET_IDLE = 0x0A,   /// Silence an Input report until the data changes
                       /// or (wValue.upper * 4) ms elapses (0 => infinite).
                       /// wValue.lower = Report ID (0 applies to all reports)
    GET_IDLE = 0x02,   /// Read the current idle rate for an Input report.
                       /// wValue.lower = Report ID (0 applies to all reports)
    // Required for Boot subclass devices only
    SET_PROTOCOL = 0x0B, /// Sets the new protocol. wValue = @ref ::hid::protocol
    GET_PROTOCOL = 0x03, /// Read which @ref ::hid::protocol is currently active.
};

namespace control
{
constexpr usb::control::request_id SET_REPORT{direction::OUT, usb::control::request::type::CLASS,
                                              usb::control::request::recipient::INTERFACE,
                                              request::SET_REPORT};

constexpr usb::control::request_id GET_REPORT{direction::IN, usb::control::request::type::CLASS,
                                              usb::control::request::recipient::INTERFACE,
                                              request::GET_REPORT};

constexpr usb::control::request_id SET_IDLE{direction::OUT, usb::control::request::type::CLASS,
                                            usb::control::request::recipient::INTERFACE,
                                            request::SET_IDLE};

constexpr usb::control::request_id GET_IDLE{direction::IN, usb::control::request::type::CLASS,
                                            usb::control::request::recipient::INTERFACE,
                                            request::GET_IDLE};

constexpr usb::control::request_id SET_PROTOCOL{direction::OUT, usb::control::request::type::CLASS,
                                                usb::control::request::recipient::INTERFACE,
                                                request::SET_PROTOCOL};

constexpr usb::control::request_id GET_PROTOCOL{direction::IN, usb::control::request::type::CLASS,
                                                usb::control::request::recipient::INTERFACE,
                                                request::GET_PROTOCOL};
} // namespace control

enum class country_code : uint8_t
{
    NOT_SUPPORTED = 0,
    ARABIC = 1,
    BELGIAN = 2,
    CANADIAN_BILINGUAL = 3,
    CANADIAN_FRENCH = 4,
    CZECH_REPUBLIC = 5,
    DANISH = 6,
    FINNISH = 7,
    FRENCH = 8,
    GERMAN = 9,
    GREEK = 10,
    HEBREW = 11,
    HUNGARY = 12,
    INTERNATIONAL_ISO = 13,
    ITALIAN = 14,
    JAPAN_KATAKANA = 15,
    KOREAN = 16,
    LATIN_AMERICAN = 17,
    NETHERLANDS_DUTCH = 18,
    NORWEGIAN = 19,
    PERSIAN_FARSI = 20,
    POLAND = 21,
    PORTUGUESE = 22,
    RUSSIA = 23,
    SLOVAKIA = 24,
    SPANISH = 25,
    SWEDISH = 26,
    SWISS_FRENCH = 27,
    SWISS_GERMAN = 28,
    SWITZERLAND = 29,
    TAIWAN = 30,
    TURKISH_Q = 31,
    UK = 32,
    US = 33,
    YUGOSLAVIA = 34,
    TURKISH_F = 35,
};

namespace descriptor
{
/// @brief HID class descriptor types
enum class type : uint8_t
{
    HID = 0x21,      /// HID class descriptor
    REPORT = 0x22,   /// HID report descriptor
    PHYSICAL = 0x23, /// HID physical descriptor
};

struct class_subdescriptor
{
    type bDescriptorType{type::REPORT};
    le_uint16_t wItemLength;
};

template <size_t CLASS_DESC_COUNT = 1>
struct hid : public usb::descriptor<hid<CLASS_DESC_COUNT>>
{
    constexpr static auto TYPE_CODE = type::HID;

    version bcdHID{SPEC_VERSION};
    country_code bCountryCode{country_code::NOT_SUPPORTED};
    uint8_t bNumDescriptors{CLASS_DESC_COUNT};
    class_subdescriptor ClassDescriptors[CLASS_DESC_COUNT]{};
};
} // namespace descriptor
} // namespace usb::hid

#endif // __USB_CLASS_HID_HPP_
