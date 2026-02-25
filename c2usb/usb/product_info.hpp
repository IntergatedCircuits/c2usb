/// @file
///
/// @author Benedek Kupper
/// @date   2025
///
/// @copyright
///         This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
///         If a copy of the MPL was not distributed with this file, You can obtain one at
///         https://mozilla.org/MPL/2.0/.
///
#ifndef __USB_PRODUCT_INFO_HPP_
#define __USB_PRODUCT_INFO_HPP_

#include "usb/version.hpp"
#include <string_view>

namespace usb
{
/// @brief  The product_info class stores USB device product information.
struct product_info
{
    const char_t* vendor_name{};
    const char_t* product_name{};
    uint16_t vendor_id;
    uint16_t product_id;
    version product_version;

    // not for direct access, but necessary to be public to be used as a template parameter
    int16_t _serial_number_signed_size{};
    const void* _serial_number{};

    constexpr product_info(uint16_t vend_id, uint16_t pr_id, version pr_ver)
        : vendor_id(vend_id), product_id(pr_id), product_version(pr_ver)
    {}

    constexpr product_info(uint16_t vend_id, const char_t* vend_name, uint16_t pr_id,
                           const char_t* pr_name, version pr_ver,
                           const std::span<const uint8_t>& serial_number = {})
        : vendor_name(vend_name),
          product_name(pr_name),
          vendor_id(vend_id),
          product_id(pr_id),
          product_version(pr_ver),
          _serial_number_signed_size(static_cast<int16_t>(serial_number.size())),
          _serial_number(static_cast<const void*>(serial_number.data()))
    {}

    constexpr product_info(uint16_t vend_id, const char_t* vend_name, uint16_t pr_id,
                           const char_t* pr_name, version pr_ver,
                           const std::string_view& serial_number)
        : vendor_name(vend_name),
          product_name(pr_name),
          vendor_id(vend_id),
          product_id(pr_id),
          product_version(pr_ver),
          _serial_number_signed_size(-static_cast<int16_t>(serial_number.size())),
          _serial_number(static_cast<const void*>(serial_number.data()))
    {}

    constexpr bool has_serial_number() const { return _serial_number_signed_size != 0; }
    constexpr std::variant<std::string_view, std::span<const uint8_t>> serial_number() const
    {
        if (_serial_number_signed_size >= 0)
        {
            return std::span<const uint8_t>(static_cast<const uint8_t*>(_serial_number),
                                            static_cast<size_t>(_serial_number_signed_size));
        }
        else
        {
            return std::string_view(static_cast<const char*>(_serial_number),
                                    static_cast<size_t>(-_serial_number_signed_size));
        }
    }
};
} // namespace usb

#endif // __USB_PRODUCT_INFO_HPP_
