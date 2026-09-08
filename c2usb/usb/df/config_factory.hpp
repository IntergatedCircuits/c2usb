// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "usb/df/config.hpp"
#include "usb/speeds.hpp"
#include <memory_resource>

namespace usb::df::config
{
struct detail
{
    constexpr static size_t join_elements(const uint8_t* chunk_sizes, const element** chunks,
                                          element* out)
    {
        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        element* begin = out;
        while (*chunk_sizes != 0)
        {
            for (uint8_t i = 0; i < *chunk_sizes; i++)
            {
                *out = (*chunks)[i];
                out++;
            }
            chunks++;
            chunk_sizes++;
        }
        // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        return static_cast<size_t>(std::distance(begin, out));
    }

#ifdef __cpp_lib_bit_cast
    constexpr
#endif
        static void
        assign_element_array(const header& info, const uint8_t* chunk_sizes, const element** chunks,
                             element* out)
    {
        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        // add the {interface, endpoint} chunks
        auto config_size = 1 + join_elements(chunk_sizes, chunks, out + 1);

        // first element is the info header
        auto inf = info;
        inf.set_size(config_size);
        out[0] = inf;

        // finally, a terminating footer
        out[config_size] = {};
        // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }

  private:
    detail() = default;
};

/// @brief Join element arrays into a single contiguous array.
/// @tparam ...SIZES deduced template parameter
/// @param ...chunks element arrays to join
/// @return element array containing the input arrays in a single sequence
template <size_t... SIZES>
constexpr elements<(SIZES + ...)> join_elements(elements<SIZES>... chunks)
{
    constexpr uint8_t array_count = sizeof...(chunks);
    constexpr std::array<uint8_t, array_count + 1> array_lengths = {chunks.size()..., 0};
    std::array<const element*, array_count> arrays = {chunks.data()...};

    elements<(SIZES + ...)> final_array;
    detail::join_elements(array_lengths.data(), arrays.data(), final_array.data());
    return final_array;
}

/// @brief Creates the configuration array from the input header and list of elements.
/// @tparam ...SIZES: deduced template parameter
/// @param info: the configuration's base information
/// @param ...chunks: the parts of the configuration (@ref interface s and @ref endpoint s)
///                   bound to sub-arrays by calling @ref std::to_array<element>(...)
/// @return The finished configuration array that can be used via @ref view
template <size_t... SIZES>
constexpr auto make_config(const header& info, elements<SIZES>... chunks)
    -> elements<1 + (SIZES + ...) + 1>
    requires((1 + (SIZES + ...) + 1) <= std::numeric_limits<uint8_t>::max())
{
    constexpr uint8_t array_count = sizeof...(chunks);
    constexpr std::array<uint8_t, array_count + 1> array_lengths = {chunks.size()..., 0};
    std::array<const element*, array_count> arrays = {chunks.data()...};

    elements<1 + (SIZES + ...) + 1> final_array;
    detail::assign_element_array(info, array_lengths.data(), arrays.data(), final_array.data());
    return final_array;
}

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
