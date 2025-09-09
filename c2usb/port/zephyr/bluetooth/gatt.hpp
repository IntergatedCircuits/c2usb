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
#ifndef __PORT_ZEPHYR_BLUETOOTH_GATT_HPP_
#define __PORT_ZEPHYR_BLUETOOTH_GATT_HPP_

#define C2USB_HAS_ZEPHYR_BT_GATT_HEADERS __has_include("zephyr/bluetooth/gatt.h")
#if C2USB_HAS_ZEPHYR_BT_GATT_HEADERS

#include <bit>
#include <span>
#include <magic_enum.hpp>
#include <zephyr/bluetooth/gatt.h>

#include "sized_unsigned.hpp"

namespace bluetooth::zephyr
{
using uuid = ::bt_uuid;

inline bool operator==(const uuid& u1, const uuid& u2)
{
    return bt_uuid_cmp(&u1, &u2) == 0;
}

/// @brief  16-bit UUID structure definition
/// @tparam UUID_CODE the uuid code, usually zephyr's _VAL suffixed macro, e.g. BT_UUID_BAS_VAL
/// @return reference to the full uuid16 structure in rodata
template <uint16_t UUID_CODE>
inline const uuid& uuid16()
{
    static const ::bt_uuid_16 u BT_UUID_INIT_16(UUID_CODE);
    return u.uuid;
}

} // namespace bluetooth::zephyr

// https://docs.zephyrproject.org/latest/connectivity/bluetooth/api/gatt.html
namespace bluetooth::zephyr::gatt
{
enum class permissions : uint16_t
{
    NONE = ::bt_gatt_perm::BT_GATT_PERM_NONE,
    READ = ::bt_gatt_perm::BT_GATT_PERM_READ,
    WRITE = ::bt_gatt_perm::BT_GATT_PERM_WRITE,
    READ_ENCRYPT = ::bt_gatt_perm::BT_GATT_PERM_READ_ENCRYPT,
    WRITE_ENCRYPT = ::bt_gatt_perm::BT_GATT_PERM_WRITE_ENCRYPT,
    READ_AUTHEN = ::bt_gatt_perm::BT_GATT_PERM_READ_AUTHEN,
    WRITE_AUTHEN = ::bt_gatt_perm::BT_GATT_PERM_WRITE_AUTHEN,
    PREPARE_WRITE = ::bt_gatt_perm::BT_GATT_PERM_PREPARE_WRITE,
    READ_LESC = ::bt_gatt_perm::BT_GATT_PERM_READ_LESC,
    WRITE_LESC = ::bt_gatt_perm::BT_GATT_PERM_WRITE_LESC,
};

enum class properties : uint8_t
{
    NONE = 0,
    BROADCAST = BT_GATT_CHRC_BROADCAST,
    READ = BT_GATT_CHRC_READ,
    WRITE_WITHOUT_RESP = BT_GATT_CHRC_WRITE_WITHOUT_RESP,
    WRITE = BT_GATT_CHRC_WRITE,
    NOTIFY = BT_GATT_CHRC_NOTIFY,
    INDICATE = BT_GATT_CHRC_INDICATE,
    AUTH = BT_GATT_CHRC_AUTH,
    EXT_PROP = BT_GATT_CHRC_EXT_PROP,
};

enum class write_flags : uint8_t
{
    NONE = 0,
    PREPARE = BT_GATT_WRITE_FLAG_PREPARE,
    COMMAND = BT_GATT_WRITE_FLAG_CMD,
    EXECUTE = BT_GATT_WRITE_FLAG_EXECUTE,
};

class attribute;

enum class ccc_flags : uint16_t
{
    NONE = 0,
    NOTIFY = BT_GATT_CCC_NOTIFY,
    INDICATE = BT_GATT_CCC_INDICATE,
};

#ifdef BT_GATT_CCC_MANAGED_USER_DATA_INIT
using ccc_store_base = ::bt_gatt_ccc_managed_user_data;
#else
using ccc_store_base = ::_bt_gatt_ccc;
#endif

/// @brief This class stores the context information for GATT CCC descriptors,
///        which have different value per connected client.
class ccc_store : public ccc_store_base
{
  public:
    constexpr ccc_store()
        : ccc_store_base()
    {}

    ccc_store(void (*changed)(const attribute*, ccc_flags),
              ssize_t (*cfg_write)(::bt_conn*, const attribute*, ccc_flags),
              bool (*cfg_match)(::bt_conn*, const attribute*) = nullptr)
        : ccc_store_base{
              .cfg_changed = reinterpret_cast<void (*)(const ::bt_gatt_attr*, uint16_t)>(changed),
              .cfg_write =
                  reinterpret_cast<ssize_t (*)(::bt_conn*, const ::bt_gatt_attr*, uint16_t)>(
                      cfg_write),
              .cfg_match = reinterpret_cast<bool (*)(::bt_conn*, const ::bt_gatt_attr*)>(cfg_match)}
    {}

  protected:
    constexpr size_t cfg_size() const { return sizeof(cfg) / sizeof(cfg[0]); }
};

/// @brief This class stores the GATT characteristic declaration information.
class char_decl : public ::bt_gatt_chrc
{
  public:
    constexpr char_decl(const zephyr::uuid& id, gatt::properties props, uint16_t handle = 0)
        : bt_gatt_chrc{.uuid = &id,
                       .value_handle = handle,
                       .properties =
                           static_cast<decltype(std::declval<::bt_gatt_chrc>().properties)>(props)}
    {}
};

/// @brief This class encapsulates the generic GATT attribute functionality.
class attribute : public ::bt_gatt_attr
{
  public:
    using read_fn = ssize_t (*)(::bt_conn*, const attribute*, uint8_t*, uint16_t, uint16_t);
    using write_fn = ssize_t (*)(::bt_conn*, const attribute*, const uint8_t*, uint16_t, uint16_t,
                                 write_flags);

    attribute()
        : bt_gatt_attr()
    {}

    template <typename T>
    attribute(const zephyr::uuid& uuid, gatt::permissions perm, ::bt_gatt_attr_read_func_t read,
              ::bt_gatt_attr_write_func_t write, T* user_data = nullptr)
        : bt_gatt_attr{.uuid = &uuid,
                       .read = read,
                       .write = write,
                       .user_data =
                           reinterpret_cast<void*>(const_cast<std::remove_const_t<T>*>(user_data)),
                       .handle = 0,
                       .perm = static_cast<std::underlying_type_t<decltype(perm)>>(perm)}
    {}

    template <typename T>
    attribute(const zephyr::uuid& uuid, gatt::permissions perm, read_fn read, write_fn write,
              T* user_data = nullptr)
        : bt_gatt_attr{.uuid = &uuid,
                       .read = reinterpret_cast<::bt_gatt_attr_read_func_t>(read),
                       .write = reinterpret_cast<::bt_gatt_attr_write_func_t>(write),
                       .user_data =
                           static_cast<void*>(const_cast<std::remove_const_t<T>*>(user_data)),
                       .handle = 0,
                       .perm = static_cast<std::underlying_type_t<decltype(perm)>>(perm)}
    {}

    template <typename T>
    attribute(const zephyr::uuid& uuid, gatt::permissions perm, T user_value)
        : bt_gatt_attr{
              .uuid = &uuid,
              .read = reinterpret_cast<::bt_gatt_attr_read_func_t>(&attribute::read_value<T>),
              .write = nullptr,
              .user_data =
                  reinterpret_cast<void*>(std::bit_cast<sized_unsigned_t<sizeof(T)>>(user_value)),
              .handle = 0,
              .perm = static_cast<std::underlying_type_t<decltype(perm)>>(perm)}
    {}

    template <typename T>
    T user_value() const
    {
        return std::bit_cast<T>(
            static_cast<sized_unsigned_t<sizeof(T)>>(reinterpret_cast<std::uintptr_t>(user_data)));
    }

    template <typename T>
    static ssize_t read_value(::bt_conn* conn, const attribute* attr, uint8_t* buf, uint16_t len,
                              uint16_t offset)
    {
        auto value = attr->user_value<T>();
        return bt_gatt_attr_read(conn, attr, static_cast<void*>(buf), len, offset, &value,
                                 sizeof(T));
    }

    template <typename T>
    static ssize_t read_range(::bt_conn* conn, const attribute* attr, uint8_t* buf, uint16_t len,
                              uint16_t offset)
    {
        auto* range = static_cast<T*>(attr->user_data);

        return bt_gatt_attr_read(conn, attr, static_cast<void*>(buf), len, offset, range->data(),
                                 range->size());
    }

    template <size_t SIZE>
    static ssize_t read_static_data(::bt_conn* conn, const attribute* attr, uint8_t* buf,
                                    uint16_t len, uint16_t offset)
    {
        return bt_gatt_attr_read(conn, attr, static_cast<void*>(buf), len, offset, attr->user_data,
                                 SIZE);
    }

    bool ccc_active(::bt_conn* conn, ccc_flags flag) const
    {
        return bt_gatt_is_subscribed(conn, this, static_cast<uint16_t>(flag));
    }

    template <typename T>
    int notify(const std::span<const uint8_t>& data, ::bt_gatt_complete_func_t cb,
               const T* user_data = nullptr, ::bt_conn* conn = nullptr) const
    {
        ::bt_gatt_notify_params params{.attr = this,
                                       .data = static_cast<const void*>(data.data()),
                                       .len = static_cast<uint16_t>(data.size()),
                                       .func = cb,
                                       .user_data = static_cast<void*>(const_cast<T*>(user_data))};
        return bt_gatt_notify_cb(conn, &params);
    }

    int notify(const std::span<const uint8_t>& data, ::bt_conn* conn = nullptr) const
    {
        return notify<std::nullptr_t>(data, nullptr, nullptr, conn);
    }

    /// @brief This class provides a convenience to initialize a GATT attributes array.
    class builder
    {
      public:
        explicit builder(attribute* attr)
            : attr_(attr)
        {}

        attribute* data() { return attr_; }

        /// @brief Creates a primary service attribute.
        /// @param uuid service type UUID
        /// @return builder to continue setting the attribute array
        builder primary_service(const zephyr::uuid& uuid)
        {
            *attr_ = attribute(uuid16<BT_UUID_GATT_PRIMARY_VAL>(), permissions::READ,
                               bt_gatt_attr_read_service, nullptr, &uuid);
            return builder(attr_ + 1);
        }

        /// @brief Creates a characteristic using two attributes.
        /// @tparam T user data's deduced type
        /// @tparam TRead read method's deduced type
        /// @tparam TWrite write method's deduced type
        /// @param info characteristic declaration
        /// @param perm characteristic value attribute access permissions
        /// @param read the reader method gets called by GATT to get the characteristic value
        /// @param write the writer method gets called by GATT to set the characteristic value
        /// @param user_data context pointer
        /// @return builder to continue setting the attribute array
        template <typename T, typename TRead, typename TWrite>
        builder characteristic(const char_decl& info, permissions perm, TRead read, TWrite write,
                               T* user_data = nullptr)
        {
            *attr_ = attribute(uuid16<BT_UUID_GATT_CHRC_VAL>(), permissions::READ,
                               bt_gatt_attr_read_chrc, nullptr, &info);
            *(attr_ + 1) = attribute(*info.uuid, perm, read, write, user_data);
            return builder(attr_ + 2);
        }

        /// @brief Creates a characteristic with a fixed value, using two attributes.
        /// @tparam T the fixed value's deduced type
        /// @param info characteristic declaration
        /// @param perm characteristic value attribute access permissions
        /// @param value fixed value
        /// @return builder to continue setting the attribute array
        template <typename T>
        builder characteristic(const char_decl& info, permissions perm, T value)
        {
            *attr_ = attribute(uuid16<BT_UUID_GATT_CHRC_VAL>(), permissions::READ,
                               bt_gatt_attr_read_chrc, nullptr, &info);
            *(attr_ + 1) = attribute(*info.uuid, perm, value);
            return builder(attr_ + 2);
        }

        /// @brief Creates a descriptor with a fixed value.
        /// @tparam T the fixed value's deduced type
        /// @param uuid descriptor type UUID
        /// @param perm descriptor attribute access permissions
        /// @param value fixed value
        /// @return builder to continue setting the attribute array
        template <typename T>
        builder descriptor(const zephyr::uuid& uuid, permissions perm, T value)
        {
            *attr_ = attribute(uuid, perm, value);
            return builder(attr_ + 1);
        }

        /// @brief Creates a Client Characteristic Configuration descriptor.
        /// @param ccc the CCC context object
        /// @param perm descriptor attribute access permissions
        /// @return builder to continue setting the attribute array
        builder ccc_descriptor(ccc_store& ccc, permissions perm)
        {
            *attr_ = attribute(uuid16<BT_UUID_GATT_CCC_VAL>(), perm, bt_gatt_attr_read_ccc,
                               bt_gatt_attr_write_ccc, &ccc);
            return builder(attr_ + 1);
        }

      private:
        attribute* attr_;
    };
};

/// @brief This class manages the registration of GATT services.
///        Requires the CONFIG_BT_GATT_DYNAMIC_DB to be set.
class service : public ::bt_gatt_service
{
    bool registered_{};

  public:
    constexpr service(const std::span<gatt::attribute>& attrs)
        : bt_gatt_service{.attrs = attrs.data(), .attr_count = attrs.size()}
    {}
    bool active() const { return registered_; }
    bool start()
    {
        if (!registered_)
        {
            registered_ = bt_gatt_service_register(this) == 0;
        }
        return registered_;
    }
    bool stop()
    {
        if (registered_)
        {
            registered_ = !(bt_gatt_service_unregister(this) == 0);
        }
        return !registered_;
    }
    ~service() { stop(); }
};

} // namespace bluetooth::zephyr::gatt

template <>
struct magic_enum::customize::enum_range<bluetooth::zephyr::gatt::permissions>
{
    static constexpr bool is_flags = true;
};
template <>
struct magic_enum::customize::enum_range<bluetooth::zephyr::gatt::properties>
{
    static constexpr bool is_flags = true;
};
template <>
struct magic_enum::customize::enum_range<bluetooth::zephyr::gatt::write_flags>
{
    static constexpr bool is_flags = true;
};
template <>
struct magic_enum::customize::enum_range<bluetooth::zephyr::gatt::ccc_flags>
{
    static constexpr bool is_flags = true;
};

#endif // C2USB_HAS_ZEPHYR_BT_GATT_HEADERS

#endif // __PORT_ZEPHYR_BLUETOOTH_GATT_HPP_
