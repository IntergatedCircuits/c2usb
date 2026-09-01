// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "usb/df/config.hpp"
#include "usb/speeds.hpp"
#include <memory_resource>

namespace usb::df::config
{
/// @brief  Creates a configuration array in the supplied memory resource.
/// @note   The memory resource and allocated storage must outlive the returned view.
/// @tparam ...SIZES deduced template parameter
/// @param  resource: memory resource to allocate the configuration array from
/// @param  info: the configuration's base information
/// @param  ...chunks element arrays to join
/// @return The view to the allocated configuration array
template <size_t... SIZES>
[[nodiscard]] view make_config(std::pmr::memory_resource* resource, const header& info,
                               elements<SIZES>... chunks)
    requires((1 + (SIZES + ...) + 1) <= std::numeric_limits<uint8_t>::max())
{
    std::pmr::polymorphic_allocator<element> allocator(resource);
    auto* final_array = allocator.allocate(1 + (SIZES + ...) + 1);
    assert(final_array != nullptr);

    constexpr uint8_t array_count = sizeof...(chunks);
    constexpr std::array<uint8_t, array_count + 1> array_lengths = {chunks.size()..., 0};
    std::array<const element*, array_count> arrays = {chunks.data()...};
    detail::assign_element_array(info, array_lengths.data(), arrays.data(), final_array);

    return view(final_array);
}

/// @brief  Storage for configuration arrays using std::pmr::monotonic_buffer_resource.
/// @tparam SPEEDS: The speeds supported by the configuration set.
/// @tparam MAX_SIZE: The maximum configuration size in the set.
template <usb::speeds SPEEDS, size_t MAX_SIZE>
class monotonic_storage
{
  public:
    constexpr monotonic_storage() = default;

    [[nodiscard]] constexpr std::pmr::memory_resource* resource() { return &resource_; }

    [[nodiscard]] static constexpr size_t max_size() { return MAX_SIZE; }
    [[nodiscard]] constexpr static auto speeds() { return SPEEDS; }

  private:
    alignas(element) std::array<std::byte, sizeof(elements<MAX_SIZE>) * SPEEDS.count()> storage_{};
    std::pmr::monotonic_buffer_resource resource_{storage_.data(), storage_.size()};
};

} // namespace usb::df::config
