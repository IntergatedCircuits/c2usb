// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cassert>
#include "enum_iterator.hpp"
#include "usb/base.hpp"

namespace usb
{
/// @brief  The speeds class stores a range of USB speeds.
class speeds
{
    using numeric_t = std::underlying_type_t<speed>;

  public:
    using iterator = enum_iterator<speed>;

    constexpr speeds(speed mini, speed maxi)
        : min(mini), max(maxi)
    {
        assert(min <= max);
        assert(speed::NONE < min);
    }
    constexpr speeds(speed single)
        : min(single), max(single)
    {
        assert(speed::NONE < min);
    }
    speed min;
    speed max;
    [[nodiscard]] constexpr iterator begin() const { return min; }
    [[nodiscard]] constexpr iterator end() const
    {
        return static_cast<speed>(static_cast<numeric_t>(max) + 1);
    }
    [[nodiscard]] constexpr bool includes(speed s) const { return (min <= s) and (s <= max); }
    [[nodiscard]] constexpr bool includes(speeds ss) const
    {
        return (min <= ss.min) and (ss.max <= max);
    }
    [[nodiscard]] constexpr size_t count() const
    {
        return size_t(1 + static_cast<numeric_t>(max) - static_cast<numeric_t>(min));
    }
    [[nodiscard]] constexpr size_t offset(speed s) const
    {
        return size_t(static_cast<numeric_t>(s) - static_cast<numeric_t>(min));
    }
    [[nodiscard]] constexpr speed at(size_t index) const
    {
        return static_cast<speed>(static_cast<numeric_t>(min) + numeric_t(index));
    }
};
} // namespace usb
