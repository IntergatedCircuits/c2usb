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
#ifndef __USB_DF_TRANSFER_HPP_
#define __USB_DF_TRANSFER_HPP_

#include "usb/base.hpp"

namespace usb::df
{
/// @brief  The transfer class stores information of a single USB transfer.
class transfer
{
  public:
    using size_type = uint16_t;

    constexpr transfer() = default;
    constexpr transfer(const uint8_t* data, size_type size)
        : data_(const_cast<uint8_t*>(data)), size_(size)
    {}
    template <typename T>
    transfer(T begin, T end)
        : data_(const_cast<uint8_t*>(std::to_address(begin))),
          size_(static_cast<size_type>(reinterpret_cast<const uint8_t*>(std::to_address(end)) -
                                       data_))
    {}
    template <typename T>
    transfer(const T& view)
        : transfer(view.begin(), view.end())
    {}
    constexpr uint8_t* data() const { return data_; }
    constexpr size_type size() const { return size_; }
    constexpr bool empty() const { return size() == 0; }

    static const transfer& stall()
    {
        static const transfer t{reinterpret_cast<uint8_t*>(UINTPTR_MAX), 0};
        return t;
    }
    bool stalled() const { return data() == stall().data(); }

    operator std::span<uint8_t>() const { return {data(), size()}; }
    operator std::span<const uint8_t>() const
    {
        return {const_cast<const uint8_t*>(data()), size()};
    }

  private:
    uint8_t* data_{};
    size_type size_{};
};

/// @brief  The ep_handle class provides an abstract value that the functions can use to interact
///         with endpoints.
class ep_handle
{
  public:
    constexpr ep_handle() {}
    constexpr operator auto() const { return id_; }
    constexpr bool valid() const { return id_ != 0; }
    constexpr auto operator<=>(const ep_handle& rhs) const = default;

  private:
    friend class mac;
    constexpr ep_handle(uint8_t i)
        : id_(i)
    {}
    uint8_t id_{};
};

// TODO: create packed ep-transfer class

} // namespace usb::df

#endif // __USB_DF_TRANSFER_HPP_
