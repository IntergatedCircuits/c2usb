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
#ifndef __USB_SPEEDS_HPP_
#define __USB_SPEEDS_HPP_

#include "usb/base.hpp"

namespace usb
{
/// @brief  The speeds class stores a range of USB speeds.
class speeds
{
    using numeric_t = std::underlying_type_t<speed>;

  public:
    class iterator
    {
      public:
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = numeric_t;
        using value_type = speed;
        using pointer = value_type*;
        using reference = value_type&;

        constexpr iterator(value_type v)
            : value_(v)
        {}
        constexpr iterator& operator++()
        {
            value_ = static_cast<speed>(static_cast<difference_type>(value_) + 1);
            return *this;
        }
        constexpr iterator operator++(int)
        {
            iterator retval = *this;
            ++(*this);
            return retval;
        }
        constexpr reference operator*() { return (value_); }
        constexpr pointer operator->() { return &(value_); }
        constexpr bool operator==(const iterator& rhs) const = default;

      private:
        value_type value_;
    };

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
    const speed min;
    const speed max;
    constexpr iterator begin() const { return min; }
    constexpr iterator end() const { return static_cast<speed>(static_cast<numeric_t>(max) + 1); }
    constexpr bool includes(speed s) const { return (min <= s) and (s <= max); }
    constexpr bool includes(speeds ss) const { return (min <= ss.min) and (ss.max <= max); }
    constexpr size_t count() const
    {
        return 1u + static_cast<numeric_t>(max) - static_cast<numeric_t>(min);
    }
    constexpr size_t offset(speed s) const
    {
        return static_cast<numeric_t>(s) - static_cast<numeric_t>(min);
    }
    constexpr speed at(size_t index) const
    {
        return static_cast<speed>(static_cast<numeric_t>(min) + index);
    }
};
} // namespace usb

#endif // __USB_SPEEDS_HPP_
