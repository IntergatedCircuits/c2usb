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
#ifndef __USB_STANDARD_REQUESTS_HPP_
#define __USB_STANDARD_REQUESTS_HPP_

#include "usb/control.hpp"

namespace usb::standard
{
enum class request : uint8_t
{
    GET_STATUS = 0x00,        /// Get current status of features
    CLEAR_FEATURE = 0x01,     /// Clear the activation of a feature
    SET_FEATURE = 0x03,       /// Activation of a feature
    SET_ADDRESS = 0x05,       /// Set the bus address of the device
    GET_DESCRIPTOR = 0x06,    /// Get a descriptor from the device
    SET_DESCRIPTOR = 0x07,    /// Write a descriptor in the device
    GET_CONFIGURATION = 0x08, /// Get the current device configuration index
    SET_CONFIGURATION = 0x09, /// Set the new device configuration index
    GET_INTERFACE = 0x0A,     /// Get the current alternate setting of the interface
    SET_INTERFACE = 0x0B,     /// Set the new alternate setting of the interface
    SYNCH_FRAME = 0x0C,
    SET_ENCRYPTION = 0x0D,
    GET_ENCRYPTION = 0x0E,
    SET_HANDSHAKE = 0x0F,
    GET_HANDSHAKE = 0x10,
    SET_CONNECTION = 0x11,
    SET_SECURITY_DATA = 0x12,
    GET_SECURITY_DATA = 0x13,
    SET_WUSB_DATA = 0x14,
    LOOPBACK_DATA_WRITE = 0x15,
    LOOPBACK_DATA_READ = 0x16,
    SET_INTERFACE_DS = 0x17,
    AUTH_IN = 0x18,
    AUTH_OUT = 0x19,
    GET_FW_STATUS = 0x1A,
    SET_FW_STATUS = 0x1B,
    SET_SEL = 0x30,
    SET_ISOCH_DELAY = 0x31,
};

namespace device
{
enum class feature : uint8_t
{
    REMOTE_WAKEUP = 0x01,
    TEST_MODE = 0x02,
    B_HNP_ENABLE = 0x03,      /// see OTG
    A_HNP_SUPPORT = 0x04,     /// see OTG
    A_ALT_HNP_SUPPORT = 0x05, /// see OTG
    WUSB_DEVICE = 0x06,

    U1_ENABLE = 0x30,
    U2_ENABLE = 0x31,
    LTM_ENABLE = 0x32,
    B3_NTF_HOST_REL = 0x33, /// see OTG
    B3_RSP_ENABLE = 0x34,   /// see OTG
    LDM_ENABLE = 0x35,
};

enum class status_type : uint8_t
{
    STANDARD = 0x00, /// Returns Standard Status Request information (wLength == 2)
    PTM =
        0x01, /// Returns PTM (Precision Time Measurement) Status Request information (wLength == 4)
};

union status
{
    uint16_t w{};
    struct
    {
        bool self_powered : 1;
        bool remote_wakeup : 1;
        bool u1_enable : 1;
        bool u2_enable : 1;
        bool ltm_enable : 1;
    };
    constexpr operator uint16_t() const { return w; }
    constexpr operator uint16_t&() { return w; };
};

union ptm_status
{
    uint32_t dw{};
    struct
    {
        bool ldm_enable : 1;
        bool ldm_valid : 1;
        uint16_t : 14;
        uint16_t ldm_link_delay : 16;
    };
    constexpr operator uint32_t() const { return dw; }
    constexpr operator uint32_t&() { return dw; };
};

constexpr control::request_id GET_STATUS{direction::IN, control::request::type::STANDARD,
                                         control::request::recipient::DEVICE, request::GET_STATUS};

constexpr control::request_id CLEAR_FEATURE{direction::OUT, control::request::type::STANDARD,
                                            control::request::recipient::DEVICE,
                                            request::CLEAR_FEATURE};

constexpr control::request_id SET_FEATURE{direction::OUT, control::request::type::STANDARD,
                                          control::request::recipient::DEVICE,
                                          request::SET_FEATURE};

constexpr control::request_id SET_ADDRESS{direction::OUT, control::request::type::STANDARD,
                                          control::request::recipient::DEVICE,
                                          request::SET_ADDRESS};

constexpr control::request_id GET_DESCRIPTOR{direction::IN, control::request::type::STANDARD,
                                             control::request::recipient::DEVICE,
                                             request::GET_DESCRIPTOR};

constexpr control::request_id SET_DESCRIPTOR{direction::OUT, control::request::type::STANDARD,
                                             control::request::recipient::DEVICE,
                                             request::SET_DESCRIPTOR};

constexpr control::request_id GET_CONFIGURATION{direction::IN, control::request::type::STANDARD,
                                                control::request::recipient::DEVICE,
                                                request::GET_CONFIGURATION};

constexpr control::request_id SET_CONFIGURATION{direction::OUT, control::request::type::STANDARD,
                                                control::request::recipient::DEVICE,
                                                request::SET_CONFIGURATION};

constexpr control::request_id SET_ISOCH_DELAY{direction::OUT, control::request::type::STANDARD,
                                              control::request::recipient::DEVICE,
                                              request::SET_ISOCH_DELAY};

constexpr control::request_id SET_SEL{direction::OUT, control::request::type::STANDARD,
                                      control::request::recipient::DEVICE, request::SET_SEL};

constexpr control::request_id SET_FW_STATUS{direction::OUT, control::request::type::STANDARD,
                                            control::request::recipient::DEVICE,
                                            request::SET_FW_STATUS};

constexpr control::request_id GET_FW_STATUS{direction::IN, control::request::type::STANDARD,
                                            control::request::recipient::DEVICE,
                                            request::GET_FW_STATUS};
} // namespace device

namespace interface
{
enum class feature : uint8_t
{
    FUNCTION_SUSPEND = 0x00,
};

union status
{
    uint16_t w{};
    struct
    {
        bool remote_wake_capable : 1;
        bool remote_wakeup : 1;
    };
    constexpr operator uint16_t() const { return w; }
    constexpr operator uint16_t&() { return w; };
};

constexpr control::request_id GET_STATUS{direction::IN, control::request::type::STANDARD,
                                         control::request::recipient::INTERFACE,
                                         request::GET_STATUS};

constexpr control::request_id CLEAR_FEATURE{direction::OUT, control::request::type::STANDARD,
                                            control::request::recipient::INTERFACE,
                                            request::CLEAR_FEATURE};

constexpr control::request_id SET_FEATURE{direction::OUT, control::request::type::STANDARD,
                                          control::request::recipient::INTERFACE,
                                          request::SET_FEATURE};

constexpr control::request_id GET_INTERFACE{direction::IN, control::request::type::STANDARD,
                                            control::request::recipient::INTERFACE,
                                            request::GET_INTERFACE};

constexpr control::request_id SET_INTERFACE{direction::OUT, control::request::type::STANDARD,
                                            control::request::recipient::INTERFACE,
                                            request::SET_INTERFACE};

constexpr control::request_id GET_DESCRIPTOR{direction::IN, control::request::type::STANDARD,
                                             control::request::recipient::INTERFACE,
                                             request::GET_DESCRIPTOR};
} // namespace interface

namespace endpoint
{
enum class feature : uint8_t
{
    HALT = 0,
};

union status
{
    uint16_t w{};
    struct
    {
        bool halt : 1;
    };
    constexpr operator uint16_t() const { return w; }
    constexpr operator uint16_t&() { return w; };
};

constexpr control::request_id GET_STATUS{direction::IN, control::request::type::STANDARD,
                                         control::request::recipient::ENDPOINT,
                                         request::GET_STATUS};

constexpr control::request_id CLEAR_FEATURE{direction::OUT, control::request::type::STANDARD,
                                            control::request::recipient::ENDPOINT,
                                            request::CLEAR_FEATURE};

constexpr control::request_id SET_FEATURE{direction::OUT, control::request::type::STANDARD,
                                          control::request::recipient::ENDPOINT,
                                          request::SET_FEATURE};

constexpr control::request_id SYNCH_FRAME{direction::IN, control::request::type::STANDARD,
                                          control::request::recipient::ENDPOINT,
                                          request::SYNCH_FRAME};
} // namespace endpoint
} // namespace usb::standard

#endif // __USB_STANDARD_REQUESTS_HPP_
