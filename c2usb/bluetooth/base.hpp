// SPDX-License-Identifier: MPL-2.0
#pragma once
#if __has_include("zephyr/bluetooth/bluetooth.h")
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#else
#error "Unsupported platform"
#endif
#include "c2usb.hpp"

namespace bluetooth
{
enum struct security : uint8_t
{
    L1_NONE = BT_SECURITY_L1,
    L2_UNAUTH_ENC = BT_SECURITY_L2, // Encryption with unauthenticated pairing
    L3_AUTH_ENC = BT_SECURITY_L3,   // Authenticated pairing with encryption
    L4_LE_SC = BT_SECURITY_L4       // Authenticated LE Secure Connections pairing with encryption
};

/// @brief Creates a string out of a Bluetooth LE address.
struct le_address_str : public std::array<char, BT_ADDR_LE_STR_LEN>
{
    le_address_str(const ::bt_conn* conn)
    {
        bt_addr_le_to_str(bt_conn_get_dst(conn), data(), size());
    }
};
} // namespace bluetooth
