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
#ifndef __USB_PRODUCT_INFO_HPP_
#define __USB_PRODUCT_INFO_HPP_

#include "usb/version.hpp"

namespace usb
{
    /// @brief  The product_info class stores USB device product information.
    struct product_info
    {
        const char_t* vendor_name {};
        const char_t* product_name {};
        std::span<const uint8_t> serial_number {};
        uint16_t vendor_id;
        uint16_t product_id;
        version product_version;

        constexpr product_info(uint16_t vendor_id, const char_t* vendor_name,
            uint16_t product_id, const char_t* product_name, version product_version,
            const std::span<const uint8_t>&& serial_no = {})
                : vendor_name(vendor_name), product_name(product_name), serial_number(std::move(serial_no)),
                  vendor_id(vendor_id), product_id(product_id), product_version(product_version)
        {}

        constexpr product_info(uint16_t vendor_id, uint16_t product_id, version product_version)
                : vendor_id(vendor_id), product_id(product_id), product_version(product_version)
        {}
    };
}

#endif // __USB_PRODUCT_INFO_HPP_
