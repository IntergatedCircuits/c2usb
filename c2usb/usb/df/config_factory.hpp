// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <vector>
#include "usb/df/config.hpp"
#include "usb/speeds.hpp"

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

/// @brief  Mutable configuration builder that allows dynamic modification of the
//          configuration array. The configuration shall not be modified while
//          it is assigned to a device.
template <class Allocator = std::allocator<element>>
class builder
{
  public:
    builder(const config::header& info, usb::speed speed, size_t reserve_size = 0,
            const Allocator& alloc = Allocator())
        : storage_(alloc), speed_(speed)
    {
        storage_.reserve(reserve_size);
        storage_.push_back(info);
        storage_.push_back(footer());
        update_config_size();
    }

    /// @brief  Returns the speed of the configuration, which shall be kept the same.
    [[nodiscard]] usb::speed speed() const { return speed_; }

    [[nodiscard]] config::view view() const { return config::view(storage_.data()); }

    [[nodiscard]] const config::header& info() const
    {
        return reinterpret_cast<const config::header&>(storage_.front());
    }

    /// @brief  An empty configuration has no interfaces or endpoints defined.
    [[nodiscard]] bool empty() const { return storage_.size() <= 2; }

    [[nodiscard]] config::power& power() { return static_cast<config::power&>(header()); }

    /// @brief  Append a set of elements (representing a function, or a set of functions) to the
    ///         configuration.
    /// @param  elems: The span of elements to append to the configuration.
    void append(const std::span<const element>& elems)
    {
        assert(!elems.empty());
        storage_.insert(storage_.end() - 1, elems.begin(), elems.end());
        update_config_size();
    }

    /// @brief  Remove a function from the configuration, including all its interfaces and
    ///         endpoints.
    /// @param  func  The function to remove
    /// @return True if the function was successfully removed, false if not found.
    bool remove(function& func)
    {
        auto view = config::view(storage_.data());
        auto first = storage_.end();
        auto last = storage_.end();
        for (const auto& iface : view.interfaces())
        {
            if ((&iface.function() == &func) and (iface.primary()))
            {
                assert(first == storage_.end());
                auto* element_ptr = reinterpret_cast<element*>(const_cast<interface*>(&iface));
                first = storage_.begin() + (element_ptr - storage_.data());
            }
            auto* element_ptr = reinterpret_cast<element*>(const_cast<interface*>(&iface));
            last = storage_.begin() + (element_ptr - storage_.data()) +
                   static_cast<std::vector<element, Allocator>::difference_type>(
                       iface.endpoints().count());
        }
        if (first != storage_.end())
        {
            assert(last != storage_.end());
            storage_.erase(first, last);
            update_config_size();
            return true;
        }
        return false;
    }

    /// @brief  Clears all interfaces and endpoints from the configuration, making it empty.
    void clear()
    {
        storage_.erase(storage_.begin() + 1, storage_.end() - 1);
        update_config_size();
    }

  private:
    [[nodiscard]] config::header& header()
    {
        return reinterpret_cast<config::header&>(storage_.front());
    }
    void update_config_size() { header().set_size(static_cast<uint8_t>(storage_.size() - 1)); }

    std::vector<element, Allocator> storage_;
    usb::speed speed_;
};

} // namespace usb::df::config
