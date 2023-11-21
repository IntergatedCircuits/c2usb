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
#ifndef __I2C_HID_STANDARD_HPP_
#define __I2C_HID_STANDARD_HPP_

#include <hid/report_protocol.hpp>

#include "i2c/base.hpp"
#include "usb/product_info.hpp"
#include "usb/version.hpp"

namespace i2c::hid
{
using version = usb::version;
using product_info = usb::product_info;

constexpr version SPEC_VERSION{"1.0"};

constexpr inline size_t REPORT_LENGTH_SIZE = sizeof(uint16_t);

/// @brief  The I2C HID descriptor is the first exchanged information,
///         describing the dynamic settings of the communication protocol.
struct descriptor
{
    le_uint16_t wHIDDescLength = sizeof(descriptor);
    version bcdVersion = SPEC_VERSION;
    le_uint16_t wReportDescLength{};
    le_uint16_t wReportDescRegister{};
    le_uint16_t wInputRegister{};
    le_uint16_t wMaxInputLength{};
    le_uint16_t wOutputRegister{};
    le_uint16_t wMaxOutputLength{};
    le_uint16_t wCommandRegister{};
    le_uint16_t wDataRegister{};
    le_uint16_t wVendorID{};
    le_uint16_t wProductID{};
    le_uint16_t wVersionID{};
    le_uint32_t reserved{};

    constexpr descriptor() {}

    constexpr descriptor& reset()
    {
        *this = descriptor();
        return *this;
    }

    constexpr descriptor& set_protocol(const ::hid::report_protocol& rp)
    {
        wReportDescLength = rp.descriptor.size();
        wMaxInputLength = REPORT_LENGTH_SIZE + rp.max_input_size;
        wMaxOutputLength = REPORT_LENGTH_SIZE + rp.max_output_size;
        return *this;
    }

    constexpr descriptor& set_product_info(const hid::product_info& pinfo)
    {
        wVendorID = pinfo.vendor_id;
        wProductID = pinfo.product_id;
        wVersionID = pinfo.product_version;
        return *this;
    }

    template <typename T>
    constexpr descriptor& set_registers()
    {
        wReportDescRegister = static_cast<uint16_t>(T::REPORT_DESCRIPTOR);
        wInputRegister = static_cast<uint16_t>(T::INPUT_REPORT);
        wOutputRegister = static_cast<uint16_t>(T::OUTPUT_REPORT);
        wCommandRegister = static_cast<uint16_t>(T::COMMAND);
        wDataRegister = static_cast<uint16_t>(T::DATA);
        return *this;
    }
};

/// @brief  I2C HID command set
enum class opcode : uint8_t
{
    // Mandatory
    RESET = 0x1,      /// Reset the device at any time
    GET_REPORT = 0x2, /// Request from HOST to DEVICE to retrieve a report (input/feature)
    SET_REPORT = 0x3, /// Request from HOST to DEVICE to set a report (output/feature)
    SET_POWER = 0x8,  /// Request from HOST to DEVICE to indicate preferred power setting

    // Optional
    GET_IDLE = 0x4, /// Request from HOST to DEVICE to retrieve the current idle rate for a report
    SET_IDLE = 0x5, /// Request from HOST to DEVICE to set the current idle rate for a report
    GET_PROTOCOL = 0x6, /// Request from HOST to DEVICE to retrieve the protocol mode
    SET_PROTOCOL = 0x7, /// Request from HOST to DEVICE to set the protocol mode
};

/// @brief  Storage class for viewing commands.
class command_view
{
  public:
    constexpr static uint8_t SHORT_REPORT_ID_LIMIT = 0xf;
    constexpr hid::opcode opcode() const { return static_cast<hid::opcode>(buffer_[1]); }
    constexpr bool is_extended() const
    {
        return (opcode() >= hid::opcode::GET_REPORT) and (opcode() <= hid::opcode::SET_IDLE) and
               ((buffer_[0] & SHORT_REPORT_ID_LIMIT) == SHORT_REPORT_ID_LIMIT);
    }
    constexpr size_t size() const { return is_extended() ? 3 : 2; }
    constexpr ::hid::report::type report_type() const
    {
        return static_cast<::hid::report::type>((buffer_[0] >> 4) & 0x3);
    }
    ::hid::report::id::type report_id() const
    {
        return is_extended() ? *(buffer_.data() + 2) : (buffer_[0] & SHORT_REPORT_ID_LIMIT);
    }
    ::hid::report::selector report_selector() const
    {
        return ::hid::report::selector(this->report_type(), this->report_id());
    }
    constexpr bool sleep() const { return (buffer_[0] & 1) != 0; }

  protected:
    constexpr command_view(hid::opcode opcode, uint8_t value)
        : buffer_{value, static_cast<uint8_t>(opcode)}
    {}
    std::array<uint8_t, 2> buffer_{};
};

/// @brief  Storage class for creating commands.
template <::hid::report::id::type REPORT_ID = 0>
class command : public command_view
{
    constexpr static bool extended() { return REPORT_ID >= SHORT_REPORT_ID_LIMIT; }

  public:
    constexpr command(hid::opcode opcode, bool sleep = false)
        : command_view(opcode, sleep)
    {}
    constexpr command(hid::opcode opcode, ::hid::report::type type)
        : command_view(opcode, (static_cast<uint8_t>(type) << 4) |
                                   std::min(REPORT_ID, SHORT_REPORT_ID_LIMIT))
    {
        if constexpr (extended())
        {
            ext_buffer_ = REPORT_ID;
        }
    }

  private:
    using conditional_buffer_t = std::conditional_t<extended(), uint8_t, std::monostate>;
    [[no_unique_address]] conditional_buffer_t ext_buffer_{};
};

/// @brief  Storage for most common command data size.
struct short_data
{
    le_uint16_t length{sizeof(short_data)};
    le_uint16_t value{};

    constexpr bool valid_size() const { return length == static_cast<uint16_t>(sizeof(*this)); }
};
} // namespace i2c::hid

#endif // __I2C_HID_STANDARD_HPP_
