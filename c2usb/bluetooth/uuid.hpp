// SPDX-License-Identifier: MPL-2.0
#pragma once

#if __has_include("zephyr/bluetooth/uuid.h")
#include <zephyr/bluetooth/uuid.h>
#elif __has_include("host/ble_uuid.h")
#include <host/ble_uuid.h>
#else
#error "Unsupported platform"
#endif
#include "c2usb.hpp"

namespace bluetooth
{
#if defined(BT_UUID_INIT_16)
using uuid = ::bt_uuid;

inline bool operator==(const uuid& u1, const uuid& u2)
{
    return ::bt_uuid_cmp(&u1, &u2) == 0;
}

/// @brief  16-bit UUID structure definition
/// @tparam UUID_CODE the uuid code, usually zephyr's _VAL suffixed macro, e.g. BT_UUID_BAS_VAL
/// @return reference to the full uuid16 structure in rodata
template <uint16_t UUID_CODE>
inline C2USB_STATIC_CONSTEXPR const uuid& uuid16()
{
    static C2USB_STATIC_CONSTEXPR const ::bt_uuid_16 u BT_UUID_INIT_16(UUID_CODE);
    return u.uuid;
}

/// @brief  128-bit UUID structure definition
/// @tparam W32 First part of the UUID (32 bits)
/// @tparam W1  Second part of the UUID (16 bits)
/// @tparam W2  Third part of the UUID (16 bits)
/// @tparam W3  Fourth part of the UUID (16 bits)
/// @tparam W48 Fifth part of the UUID (48 bits)
/// @return reference to the full uuid128 structure in rodata
template <uint32_t W32, uint16_t W1, uint16_t W2, uint16_t W3, uint64_t W48>
inline C2USB_STATIC_CONSTEXPR const uuid& uuid128()
{
    static C2USB_STATIC_CONSTEXPR const ::bt_uuid_128 u BT_UUID_INIT_128(BT_UUID_128_ENCODE(W32, W1, W2, W3, W48));
    return u.uuid;
}
#elif defined(BLE_UUID16_INIT)

using uuid = ::ble_uuid_t;

/// @brief  16-bit UUID structure definition
/// @tparam UUID_CODE the uuid code, e.g. BLE_GATT_SVC_UUID16
/// @return reference to the full uuid16 structure in rodata
template <uint16_t UUID_CODE>
inline C2USB_STATIC_CONSTEXPR const uuid& uuid16()
{
    static C2USB_STATIC_CONSTEXPR const ::ble_uuid16_t u BLE_UUID16_INIT(UUID_CODE);
    return u.u;
}
#endif

} // namespace bluetooth
