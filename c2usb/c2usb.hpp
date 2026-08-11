// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <bitfilled/integer.hpp>
#include <system_error>
#include <type_traits>

#if __has_include("autoconf.h")
#include "autoconf.h"
#elif __has_include("mcux_config.h")
#include "mcux_config.h"
#endif

#ifndef C2USB_UNUSED
#define C2USB_UNUSED(A) (void)sizeof(A)
#endif

#ifndef C2USB_USB_TRANSFER_ALIGN
#define C2USB_USB_TRANSFER_ALIGN(TYPE, ID) alignas(std::uintptr_t) TYPE ID
#endif

#define C2USB_HAS_STATIC_CONSTEXPR (__cpp_constexpr >= 202211L)
#if C2USB_HAS_STATIC_CONSTEXPR
#define C2USB_STATIC_CONSTEXPR constexpr
#else
#define C2USB_STATIC_CONSTEXPR
#endif

namespace c2usb
{
using nullptr_t = std::nullptr_t;

using size_t = std::size_t;

using uint8_t = std::uint8_t;
using uint16_t = std::uint16_t;
using uint32_t = std::uint32_t;
using uint64_t = std::uint64_t;
using int8_t = std::int8_t;
using int16_t = std::int16_t;
using int32_t = std::int32_t;
using int64_t = std::int64_t;
template <std::size_t BIT_SIZE>
using le_uint_t = bitfilled::packed_integer<std::endian::little, BIT_SIZE / 8>;
using le_uint8_t = std::uint8_t;
using le_uint16_t = le_uint_t<16>;
using le_uint32_t = le_uint_t<32>;
using le_uint64_t = le_uint_t<64>;
using le_int8_t = std::int8_t;
using le_int16_t = bitfilled::packed_integer<std::endian::little, 2, std::int16_t>;
using le_int32_t = bitfilled::packed_integer<std::endian::little, 4, std::int32_t>;
using le_int64_t = bitfilled::packed_integer<std::endian::little, 8, std::int64_t>;
template <std::size_t BIT_SIZE>
using be_uint_t = bitfilled::packed_integer<std::endian::big, BIT_SIZE / 8>;
using be_uint8_t = std::uint8_t;
using be_uint16_t = be_uint_t<16>;
using be_uint32_t = be_uint_t<32>;
using be_uint64_t = be_uint_t<64>;
using be_int8_t = std::int8_t;
using be_int16_t = bitfilled::packed_integer<std::endian::big, 2, std::int16_t>;
using be_int32_t = bitfilled::packed_integer<std::endian::big, 4, std::int32_t>;
using be_int64_t = bitfilled::packed_integer<std::endian::big, 8, std::int64_t>;

template <std::size_t SIZE>
class reserved_t
{
  public:
    constexpr reserved_t() = default;

  private:
    using value_type = std::array<uint8_t, SIZE>;
    value_type data_{};
};
template <>
class reserved_t<0>
{
  public:
    constexpr reserved_t() = default;
};

/// @brief  The result class is a convenience wrapper for standard error codes,
///         storing the negative error code as an integer.
class result
{
    int code_;

  public:
    using enum std::errc;
    constexpr result(std::errc err)
        : code_(-static_cast<int>(err))
    {}
    constexpr explicit result(int err)
        : code_(err)
    {}
    /// @brief  Returns the stored error code as a raw negative POSIX error code.
    /// @return 0 if the result is OK, or a negative POSIX error code otherwise.
    [[nodiscard]] constexpr int to_int() const { return code_; }

    [[nodiscard]] constexpr bool success() const { return code_ == 0; }

    static constexpr std::errc ok = static_cast<std::errc>(0);
    static constexpr std::errc INVALID = static_cast<std::errc>(EINVAL);
    static constexpr std::errc NO_TRANSPORT = static_cast<std::errc>(ENODEV);
    static constexpr std::errc BUSY = static_cast<std::errc>(EBUSY);
    static constexpr std::errc NO_CONNECTION = static_cast<std::errc>(ENOTCONN);
    static constexpr std::errc NO_MEMORY = static_cast<std::errc>(ENOMEM);

    constexpr bool operator==(const result& other) const = default;
};

/// @brief  The interface base class is used by interface subclasses,
///         that contain only abstract virtual functions, and no member data.
///         This library limits multiple inheritance to interface subclasses.
class interface
{
  public:
    constexpr interface() = default;
    virtual ~interface() = default;

    interface(const interface&) = delete;
    interface& operator=(const interface&) = delete;
    interface(interface&&) = delete;
    interface& operator=(interface&&) = delete;
};

/// @brief  The polymorphic base class is used by any subclass that implements polymorphism,
///         but isn't a pure interface class.
class polymorphic
{
  public:
    constexpr polymorphic() = default;
    virtual ~polymorphic() = default;

    polymorphic(const polymorphic&) = delete;
    polymorphic& operator=(const polymorphic&) = delete;
    polymorphic(polymorphic&&) = delete;
    polymorphic& operator=(polymorphic&&) = delete;
};

template <typename Type, std::size_t... sizes>
constexpr auto join(const std::array<Type, sizes>&... arrays)
{
    // https://stackoverflow.com/a/42774523
    std::array<Type, (sizes + ...)> result;
    std::size_t index{};
    ((std::copy_n(arrays.begin(), sizes, result.begin() + index), index += sizes), ...);
    return result;
}

template <typename T>
constexpr unsigned aligned_size(size_t size)
{
    return (size + sizeof(T) - 1) & ~(sizeof(T) - 1);
}

template <typename T>
concept RawMemoryStorage =
    std::is_integral_v<T> and not std::is_same_v<T, bool>; // TODO: add std::byte?

template <typename T>
concept StdLayoutType = (std::is_standard_layout_v<T> and std::is_trivially_copyable_v<T> and
                         not std::is_polymorphic_v<T> and std::is_class_v<T>) or
                        std::is_enum_v<T>;

template <typename To, RawMemoryStorage From>
    requires StdLayoutType<std::remove_pointer_t<To>> and RawMemoryStorage<From> and
             (alignof(std::remove_pointer_t<To>) <= alignof(From)) and
             (not std::is_const_v<From> or std::is_const_v<std::remove_pointer_t<To>>)
[[nodiscard]] To std_layout_cast(From* ptr) noexcept
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<To>(ptr);
}

template <typename To, StdLayoutType From>
    requires StdLayoutType<From> and RawMemoryStorage<std::remove_pointer_t<To>> and
             (alignof(std::remove_pointer_t<To>) <= alignof(From)) and
             (not std::is_const_v<From> or std::is_const_v<std::remove_pointer_t<To>>)
[[nodiscard]] To std_layout_cast(From* ptr) noexcept
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<To>(ptr);
}

template <typename To, RawMemoryStorage From>
    requires RawMemoryStorage<From> and RawMemoryStorage<std::remove_pointer_t<To>> and
             (alignof(std::remove_pointer_t<To>) <= alignof(From)) and
             (not std::is_const_v<From> or std::is_const_v<std::remove_pointer_t<To>>)
[[nodiscard]] To std_layout_cast(From* ptr) noexcept
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<To>(ptr);
}

} // namespace c2usb
