// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "usb/base.hpp"

namespace usb::df
{
/// @brief  The ep_handle class provides an abstract value that the functions can use to interact
///         with endpoints.
class ep_handle
{
  public:
    constexpr ep_handle() = default;
    constexpr operator auto() const { return id_; }
    [[nodiscard]] constexpr bool valid() const { return id_ != 0; }
    constexpr auto operator<=>(const ep_handle& rhs) const = default;

  private:
    friend class mac;
    constexpr ep_handle(uint8_t i)
        : id_(i)
    {}
    uint8_t id_{};
};

/// @brief  The transfer class stores information of a single USB transfer.
class transfer
{
  public:
    enum struct outcome : uint8_t
    {
        SUCCESS,
        STALL,
        CANCELLED,
    };
    using size_type = uint16_t;

    constexpr transfer() = default;
    // NOLINTBEGIN(cppcoreguidelines-pro-type-const-cast)
    constexpr transfer(const uint8_t* data, size_type size, outcome result = outcome::SUCCESS,
                       ep_handle eph = {})
        : data_(const_cast<uint8_t*>(data)), size_(size), result_(result), eph_(eph)
    {}
    constexpr transfer(const uint8_t* data, size_type size, bool success, ep_handle eph = {})
        : data_(const_cast<uint8_t*>(data)),
          size_(size),
          result_(success ? outcome::SUCCESS : outcome::CANCELLED),
          eph_(eph)
    {}
    // NOLINTEND(cppcoreguidelines-pro-type-const-cast)

    template <typename T>
    transfer(T begin, T end)
        : data_(const_cast<uint8_t*>(std::to_address(begin))),
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          size_(static_cast<size_type>(reinterpret_cast<const uint8_t*>(std::to_address(end)) -
                                       data_))
    {}
    template <typename T>
    transfer(const T& view)
        : transfer(view.begin(), view.end())
    {}
    [[nodiscard]] constexpr uint8_t* data() const { return data_; }
    [[nodiscard]] const uint8_t* const_data() const { return const_cast<const uint8_t*>(data_); }
    [[nodiscard]] constexpr size_type size() const { return size_; }
    [[nodiscard]] constexpr bool empty() const { return size() == 0; }
    [[nodiscard]] constexpr ep_handle endpoint() const { return eph_; }
    [[nodiscard]] constexpr bool stalled() const { return result_ == outcome::STALL; }
    [[nodiscard]] constexpr bool cancelled() const { return result_ == outcome::CANCELLED; }
    [[nodiscard]] constexpr bool success() const { return result_ == outcome::SUCCESS; }
    [[nodiscard]] constexpr size_type transferred_size() const { return success() ? size_ : 0; }
    [[nodiscard]] constexpr bool needs_zlp(size_type mps) const
    {
        return success() and (not empty()) and ((size() % mps) == 0);
    }

    static const transfer& stall()
    {
        static const transfer tf{nullptr, 0, outcome::STALL};
        return tf;
    }

    [[nodiscard]] std::span<uint8_t> to_span() const { return {data(), size()}; }
    [[nodiscard]] std::span<const uint8_t> to_const_span() const
    {
        return {const_cast<const uint8_t*>(data()), size()};
    }

  private:
    uint8_t* data_{};
    size_type size_{};
    outcome result_{outcome::SUCCESS};
    ep_handle eph_;
};

} // namespace usb::df
