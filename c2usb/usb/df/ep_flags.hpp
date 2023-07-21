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
#ifndef __USB_DF_EP_FLAGS_HPP_
#define __USB_DF_EP_FLAGS_HPP_

#include <array>
#include <atomic>
#include "usb/endpoint.hpp"

namespace usb::df
{
    /// @brief  Atomic flag store for a device's endpoints.
    class ep_flags
    {
    public:
        ep_flags() = default;

        void clear(endpoint::address addr, std::memory_order order = std::memory_order_seq_cst)
        {
            storage_[index(addr)].fetch_and(~bitmask(addr), order);
        }

        bool test_and_set(endpoint::address addr, std::memory_order order = std::memory_order_seq_cst)
        {
            return storage_[index(addr)].fetch_or(bitmask(addr), order) >> bitmask(addr) & 1;
        }

        bool test(endpoint::address addr, std::memory_order order = std::memory_order::seq_cst) const
        {
            return storage_[index(addr)].load(order) >> bitmask(addr) & 1;
        }

    private:
        using integral_type = uint32_t;
        static size_t index(endpoint::address addr)
        {
            return 0;
        }
        static integral_type bitmask(endpoint::address addr)
        {
            return 1 << ((static_cast<size_t>(addr.direction()) * 16) + addr.number());
        }
        std::array<std::atomic<integral_type>, 1> storage_ {};

        ep_flags(const ep_flags&) = delete;
        ep_flags& operator=(const ep_flags&) = delete;
    };
}

#endif // __USB_DF_EP_FLAGS_HPP_
