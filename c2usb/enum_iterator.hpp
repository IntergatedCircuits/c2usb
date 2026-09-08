// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "c2usb.hpp"

namespace c2usb
{

/// @brief   An iterator over an enum type, which is assumed to be contiguous and zero-based.
/// @tparam  T: the enum type to iterate over
template <typename T>
class enum_iterator
{
  public:
    using iterator_category = std::random_access_iterator_tag;
    // std::weakly_incrementable requires a signed difference_type, numeric_t may be unsigned
    using difference_type = std::ptrdiff_t;
    using value_type = T;
    using pointer = const value_type*;
    // std::indirectly_readable requires dereferencing through a const iterator, hence by value
    using reference = value_type;

    constexpr enum_iterator() = default;
    constexpr enum_iterator(value_type v)
        : value_(v)
    {}
    constexpr enum_iterator& operator++()
    {
        value_ = static_cast<T>(static_cast<difference_type>(value_) + 1);
        return *this;
    }
    constexpr enum_iterator operator++(int)
    {
        enum_iterator retval = *this;
        ++(*this);
        return retval;
    }
    constexpr enum_iterator& operator--()
    {
        value_ = static_cast<T>(static_cast<difference_type>(value_) - 1);
        return *this;
    }
    constexpr enum_iterator operator--(int)
    {
        enum_iterator retval = *this;
        --(*this);
        return retval;
    }
    constexpr enum_iterator& operator+=(difference_type n)
    {
        value_ = static_cast<T>(static_cast<difference_type>(value_) + n);
        return *this;
    }
    constexpr enum_iterator& operator-=(difference_type n) { return *this += -n; }
    constexpr enum_iterator operator+(difference_type n) const
    {
        enum_iterator it = *this;
        it += n;
        return it;
    }
    friend constexpr enum_iterator operator+(difference_type n, enum_iterator it)
    {
        return it += n;
    }
    constexpr enum_iterator operator-(difference_type n) const { return *this + (-n); }
    constexpr difference_type operator-(const enum_iterator& rhs) const
    {
        return static_cast<difference_type>(value_) - static_cast<difference_type>(rhs.value_);
    }
    constexpr reference operator[](difference_type n) const { return *(*this + n); }
    constexpr reference operator*() const { return value_; }
    constexpr pointer operator->() const { return &value_; }
    constexpr bool operator==(const enum_iterator& rhs) const = default;
    constexpr auto operator<=>(const enum_iterator& rhs) const = default;

  private:
    value_type value_{};
};

} // namespace c2usb
