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
#ifndef __RAW_TO_HEX_STRING_HPP_
#define __RAW_TO_HEX_STRING_HPP_

#include <span>

namespace c2usb
{
template <typename T>
constexpr std::size_t raw_to_hex_string(std::span<const uint8_t> data, std::span<T> buffer)
{
    auto trimmed_size = std::min(data.size(), buffer.size() / 2);
    data = data.subspan(0, trimmed_size);

    auto convert = [](uint8_t v)
    {
        if (v < 10)
        {
            return '0' + v;
        }
        else
        {
            return 'A' + v - 10;
        }
    };
    size_t offset = 0;
    for (auto byte : data)
    {
        buffer[offset++] = convert(byte >> 4);
        buffer[offset++] = convert(byte & 0xF);
    }
    return offset;
}
} // namespace c2usb

#endif // __RAW_TO_HEX_STRING_HPP_