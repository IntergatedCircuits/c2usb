// SPDX-License-Identifier: MPL-2.0
#pragma once
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
        return storage_[index(addr)].fetch_or(bitmask(addr), order) >> bitpos(addr) & 1;
    }

    bool test(endpoint::address addr, std::memory_order order = std::memory_order::seq_cst) const
    {
        return storage_[index(addr)].load(order) >> bitpos(addr) & 1;
    }

  private:
    using integral_type = uint32_t;
    static size_t index(endpoint::address addr) { return 0; }
    static integral_type bitpos(endpoint::address addr)
    {
        return (static_cast<size_t>(addr.direction()) * 16) + addr.number();
    }
    static integral_type bitmask(endpoint::address addr) { return 1 << bitpos(addr); }
    std::array<std::atomic<integral_type>, 1> storage_{};

    ep_flags(const ep_flags&) = delete;
    ep_flags& operator=(const ep_flags&) = delete;
};

} // namespace usb::df
