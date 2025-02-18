/// @file
///
/// @author Benedek Kupper
/// @date   2025
///
/// @copyright
///         This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
///         If a copy of the MPL was not distributed with this file, You can obtain one at
///         https://mozilla.org/MPL/2.0/.
///
#ifndef __PORT_ZEPHYR_BLUETOOTH_LE_HPP_
#define __PORT_ZEPHYR_BLUETOOTH_LE_HPP_

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

#include <array>
#include <c2usb.hpp>

namespace bluetooth::zephyr
{

/// @brief Creates a string out of a Bluetooth address.
struct address_str : public std::array<char, BT_ADDR_LE_STR_LEN>
{
    address_str(const ::bt_conn* conn) { bt_addr_le_to_str(bt_conn_get_dst(conn), data(), size()); }
};

/* Advertisement data structure:
 * length of the remaining bytes (1 byte)
 * type (1 byte)
 * data (length - 1 bytes)
 */

template <typename T>
constexpr auto ad_struct(std::uint8_t type, const T& data)
    requires(std::is_integral_v<T>)
{
    const etl::unaligned_type<T, etl::endian::little> value{data};
    std::array<std::uint8_t, 2 + sizeof(T)> elem{1 + sizeof(T), type};
    std::copy_n(value.data(), sizeof(T), elem.data() + 2);
    return elem;
}

template <typename T, std::size_t N>
constexpr auto ad_struct(std::uint8_t type, const T (&items)[N])
    requires(std::is_integral_v<T>)
{
    std::array<std::uint8_t, 2 + sizeof(T) * N> elem{1 + sizeof(T) * N, type};
    std::size_t offset = 2;
    for (const auto& data : items)
    {
        const etl::unaligned_type<T, etl::endian::little> value{data};
        std::copy_n(value.data(), sizeof(T), elem.data() + offset);
        offset += sizeof(T);
    }
    return elem;
}

template <std::size_t N>
constexpr auto ad_struct(std::uint8_t type, const char (&data)[N])
{
    std::array<std::uint8_t, 2 + N - 1> elem{1 + N - 1, type};
    for (std::size_t i = 0; i < N - 1; i++)
    {
        elem[i + 2] = data[i];
    }
    return elem;
}

/// @brief Use to join the individual advertisement structures into a single array,
/// the final data packet.
using c2usb::join;

/// @brief Counts the number of advertisement data structures in a byte array.
/// @param data the byte array containing the advertisement data packet
/// @return the number of advertisement data structures
template <std::size_t N>
constexpr std::size_t ad_struct_count(const std::array<std::uint8_t, N>& data)
{
    std::size_t count = 0;
    for (std::size_t offset = 0; offset < data.max_size();)
    {
        count++;
        offset += 1 + data[offset];
    }
    return count;
}

/// @brief Creates ::bt_data array out of the advertisement data packet,
/// that is referencing the packet's data.
/// @param bytes the advertisement data packet
/// @return the array of bt_data structures
template <std::size_t COUNT, std::size_t N>
constexpr auto to_adv_data(const std::array<std::uint8_t, N>& bytes)
{
    std::array<bt_data, COUNT> elems{};
    std::size_t offset = 0;
    for (std::size_t i = 0; i < COUNT; i++)
    {
        elems[i].type = bytes[offset + 1];
        elems[i].data_len = bytes[offset] - 1;
        elems[i].data = bytes.data() + offset + 2;
        offset += 2 + elems[i].data_len;
    }
    return elems;
}

} // namespace bluetooth::zephyr

#endif // __PORT_ZEPHYR_BLUETOOTH_LE_HPP_
