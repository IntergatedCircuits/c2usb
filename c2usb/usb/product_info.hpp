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

  public:
    constexpr product_info(uint16_t vendor_id, uint16_t product_id, version product_version)
        : vendor_id(vendor_id), product_id(product_id), product_version(product_version)
    {}

    constexpr product_info(uint16_t vendor_id, const char_t* vendor_name, uint16_t product_id,
                           const char_t* product_name, version product_version,
                           const std::span<const uint8_t>& serial_number = {})
        : vendor_name(vendor_name),
          product_name(product_name),
          vendor_id(vendor_id),
          product_id(product_id),
          product_version(product_version),
          serial_number_size_(serial_number.size()),
          serial_number_(static_cast<const void*>(serial_number.data())),
          serial_number_is_raw_(true)
    {}

    constexpr product_info(uint16_t vendor_id, const char_t* vendor_name, uint16_t product_id,
                           const char_t* product_name, version product_version,
                           const std::string_view& serial_number)
        : vendor_name(vendor_name),
          product_name(product_name),
          vendor_id(vendor_id),
          product_id(product_id),
          product_version(product_version),
          serial_number_size_(serial_number.size()),
          serial_number_(static_cast<const void*>(serial_number.data())),
          serial_number_is_raw_(false)
    {}

    constexpr bool has_serial_number() const { return serial_number_size_ > 0; }
    constexpr std::variant<std::string_view, std::span<const uint8_t>> serial_number() const
    {
        if (serial_number_is_raw_)
        {
            return std::span<const uint8_t>(static_cast<const uint8_t*>(serial_number_),
                                            serial_number_size_);
        }
        else
        {
            return std::string_view(static_cast<const char*>(serial_number_), serial_number_size_);
        }
    }

  private:
    uint16_t serial_number_size_{};
    const void* serial_number_{};
    bool serial_number_is_raw_{};
};
} // namespace usb

#endif // __USB_PRODUCT_INFO_HPP_
