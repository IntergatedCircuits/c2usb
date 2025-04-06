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
#ifndef __SINGLE_ELEM_QUEUE_HPP_
#define __SINGLE_ELEM_QUEUE_HPP_

#include <cstddef>

namespace c2usb
{
// a simplified single element queue
template <typename T>
class single_elem_queue
{
  public:
    constexpr single_elem_queue() {}
    constexpr std::size_t size() const { return 1; }
    constexpr bool empty() const { return !set_; }
    constexpr bool full() const { return set_; }
    constexpr void clear() { set_ = false; }
    constexpr bool push(const T& value)
    {
        if (full())
        {
            return false;
        }
        else
        {
            _value = value;
            set_ = true;
            return true;
        }
    }
    constexpr bool front(T& value)
    {
        if (empty())
        {
            return false;
        }
        else
        {
            value = _value;
            return true;
        }
    }
    constexpr bool peek(T& value) { return front(value); }
    constexpr bool pop(T& value)
    {
        if (empty())
        {
            return false;
        }
        else
        {
            value = _value;
            set_ = false;
            return true;
        }
    }
    constexpr bool pop()
    {
        if (empty())
        {
            return false;
        }
        else
        {
            set_ = false;
            return true;
        }
    }

  private:
    T _value{};
    bool set_{};
};
} // namespace c2usb

#endif // __SINGLE_ELEM_QUEUE_HPP_
