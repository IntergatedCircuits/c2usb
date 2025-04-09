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
#ifndef __UNINIT_STORE_HPP_
#define __UNINIT_STORE_HPP_

#include <span>

namespace c2usb
{
/// @brief  This class provides storage space for types without initialization / construction.
/// @tparam T The type to provide storage for
/// @tparam SIZE Optional size property allows for creating contiguous array storage.
template <typename T, std::size_t SIZE = 1, std::size_t ALIGN = alignof(T)>
class alignas(T) uninit_store
{
  public:
    uninit_store() {}

    T& ref() { return *reinterpret_cast<T*>(this); }
    const T& ref() const { return *reinterpret_cast<const T*>(this); }
    std::span<T> span() { return {&ref(), SIZE}; }
    std::span<const T> span() const { return {&ref(), SIZE}; }

  private:
    struct alignas(ALIGN) store
    {
        std::array<unsigned char, sizeof(T)> bytes_;
    };
    std::array<store, SIZE> items_;
};
} // namespace c2usb

#endif // __UNINIT_STORE_HPP_
