// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <span>

namespace c2usb
{
template <typename T>
constexpr std::size_t raw_to_hex_string(std::span<const std::uint8_t> data, std::span<T> buffer)
{
    auto trimmed_size = std::min(data.size(), buffer.size() / 2);
    data = data.subspan(0, trimmed_size);

    auto convert = [](std::uint8_t v)
    {
        if (v < 10)
        {
            return '0' + v;
        }
        {
            return 'A' + v - 10;
        }
    };
    std::size_t offset = 0;
    for (auto byte : data)
    {
        buffer[offset++] = convert(byte >> 4);
        buffer[offset++] = convert(byte & 0xF);
    }
    return offset;
}
} // namespace c2usb
