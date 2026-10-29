// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <algorithm>
#include <array>

namespace c2usb
{

// for a full-featured compile-time string implementation, see
// https://github.com/intel/cpp-std-extensions/blob/main/include/stdx/ct_string.hpp
template <std::size_t N>
struct ct_string
{
    consteval ct_string() = default;

    consteval ct_string(const char (&str)[N])
    {
        std::copy(std::begin(str), std::end(str), value.begin());
    }

    std::array<char, N> value{};
};

} // namespace c2usb
