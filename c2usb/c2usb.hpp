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
#ifndef __C2USB_HPP_
#define __C2USB_HPP_

#include <array>
#include <cerrno>
#include <cstdint>
#include <span>
#include <etl/unaligned_type.h>
#include <type_traits>

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
using le_uint8_t = std::uint8_t;
using le_uint16_t = etl::le_uint16_t;
using le_uint32_t = etl::le_uint32_t;
using le_uint64_t = etl::le_uint64_t;
using le_int8_t = std::int8_t;
using le_int16_t = etl::le_int16_t;
using le_int32_t = etl::le_int32_t;
using le_int64_t = etl::le_int64_t;
using be_uint8_t = std::uint8_t;
using be_uint16_t = etl::be_uint16_t;
using be_uint32_t = etl::be_uint32_t;
using be_uint64_t = etl::be_uint64_t;
using be_int8_t = std::int8_t;
using be_int16_t = etl::be_int16_t;
using be_int32_t = etl::be_int32_t;
using be_int64_t = etl::be_int64_t;

enum class result : int
{
    OK = 0,
    INVALID = -EINVAL,
    NO_TRANSPORT = -ENODEV,
    BUSY = -EBUSY,
    NO_CONNECTION = -ENOTCONN,
};

/// @brief  Lookup unsigned integer of matching size
template <std::size_t SIZE, class T = void>
struct sized_unsigned
{};

template <std::size_t SIZE>
struct sized_unsigned<SIZE, std::enable_if_t<SIZE == 1>>
{
    typedef std::uint8_t type;
};
template <std::size_t SIZE>
struct sized_unsigned<SIZE, std::enable_if_t<SIZE == 2>>
{
    typedef std::uint16_t type;
};
template <std::size_t SIZE>
struct sized_unsigned<SIZE, std::enable_if_t<SIZE == 4>>
{
    typedef std::uint32_t type;
};
template <std::size_t SIZE>
struct sized_unsigned<SIZE, std::enable_if_t<SIZE == 8>>
{
    typedef std::uint64_t type;
};

template <std::size_t SIZE>
using sized_unsigned_t = typename sized_unsigned<SIZE>::type;

template <typename T>
concept IntegerConvertable = std::is_convertible_v<T, sized_unsigned_t<sizeof(T)>>;
} // namespace c2usb

#endif // __C2USB_HPP_
