// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <bit>
#include <optional>
#include <span>
#if __has_include("zephyr/bluetooth/gatt.h")
#include <zephyr/bluetooth/gatt.h>
#else
#error "Unsupported platform"
#endif
#include "bluetooth/base.hpp"
#include "bluetooth/uuid.hpp"
#include "c2usb.hpp"
#include <magic_enum.hpp>
#include <sized_unsigned.hpp>

namespace bluetooth::att
{
enum error : uint8_t
{
    SUCCESS = 0x00,
    INVALID_HANDLE = 0x01,
    READ_NOT_PERMITTED = 0x02,
    WRITE_NOT_PERMITTED = 0x03,
    INVALID_PDU = 0x04,
    AUTHENTICATION = 0x05,
    NOT_SUPPORTED = 0x06,
    INVALID_OFFSET = 0x07,
    AUTHORIZATION = 0x08,
    PREPARE_QUEUE_FULL = 0x09,
    ATTRIBUTE_NOT_FOUND = 0x0a,
    ATTRIBUTE_NOT_LONG = 0x0b,
    ENCRYPTION_KEY_SIZE = 0x0c,
    INVALID_ATTRIBUTE_LEN = 0x0d,
    UNLIKELY = 0x0e,
    INSUFFICIENT_ENCRYPTION = 0x0f,
    UNSUPPORTED_GROUP_TYPE = 0x10,
    INSUFFICIENT_RESOURCES = 0x11,
    DB_OUT_OF_SYNC = 0x12,
    VALUE_NOT_ALLOWED = 0x13,
    WRITE_REQ_REJECTED = 0xfc,
    CCC_IMPROPER_CONF = 0xfd,
    PROCEDURE_IN_PROGRESS = 0xfe,
    OUT_OF_RANGE = 0xff
};

inline ssize_t error_to_ssize(std::optional<error> err, size_t len)
{
    return err.value_or(att::error::SUCCESS) == att::error::SUCCESS ? static_cast<ssize_t>(len)
                                                                    : BT_GATT_ERR(err.value());
}

} // namespace bluetooth::att

// https://docs.zephyrproject.org/latest/connectivity/bluetooth/api/gatt.html
namespace bluetooth::gatt
{
enum class permissions : uint16_t
{
    NONE = ::bt_gatt_perm::BT_GATT_PERM_NONE,
    READ = ::bt_gatt_perm::BT_GATT_PERM_READ,
    WRITE = ::bt_gatt_perm::BT_GATT_PERM_WRITE,
    PREPARE_WRITE = ::bt_gatt_perm::BT_GATT_PERM_PREPARE_WRITE,
    READ_ENCRYPT = ::bt_gatt_perm::BT_GATT_PERM_READ_ENCRYPT,
    WRITE_ENCRYPT = ::bt_gatt_perm::BT_GATT_PERM_WRITE_ENCRYPT,
    READ_AUTHEN = ::bt_gatt_perm::BT_GATT_PERM_READ_AUTHEN,
    WRITE_AUTHEN = ::bt_gatt_perm::BT_GATT_PERM_WRITE_AUTHEN,
    READ_LESC = ::bt_gatt_perm::BT_GATT_PERM_READ_LESC,
    WRITE_LESC = ::bt_gatt_perm::BT_GATT_PERM_WRITE_LESC,
};

constexpr permissions readable_at(security level)
{
    switch (level)
    {
    case security::L1_NONE:
        return permissions::READ;
    case security::L2_UNAUTH_ENC:
        return permissions::READ_ENCRYPT;
    case security::L3_AUTH_ENC:
        return permissions::READ_AUTHEN;
    case security::L4_LE_SC:
        return permissions::READ_LESC;
    default:
        return permissions::NONE;
    }
}

constexpr permissions writeable_at(security level)
{
    switch (level)
    {
    case security::L1_NONE:
        return permissions::WRITE;
    case security::L2_UNAUTH_ENC:
        return permissions::WRITE_ENCRYPT;
    case security::L3_AUTH_ENC:
        return permissions::WRITE_AUTHEN;
    case security::L4_LE_SC:
        return permissions::WRITE_LESC;
    default:
        return permissions::NONE;
    }
}

constexpr permissions readwriteable_at(security level)
{
    switch (level)
    {
    case security::L1_NONE:
        return static_cast<permissions>(static_cast<uint16_t>(permissions::READ) |
                                        static_cast<uint16_t>(permissions::WRITE));
    case security::L2_UNAUTH_ENC:
        return static_cast<permissions>(static_cast<uint16_t>(permissions::READ_ENCRYPT) |
                                        static_cast<uint16_t>(permissions::WRITE_ENCRYPT));
    case security::L3_AUTH_ENC:
        return static_cast<permissions>(static_cast<uint16_t>(permissions::READ_AUTHEN) |
                                        static_cast<uint16_t>(permissions::WRITE_AUTHEN));
    case security::L4_LE_SC:
        return static_cast<permissions>(static_cast<uint16_t>(permissions::READ_LESC) |
                                        static_cast<uint16_t>(permissions::WRITE_LESC));
    default:
        return permissions::NONE;
    }
}

enum class properties : uint8_t
{
    NONE = 0,
    BROADCAST = BT_GATT_CHRC_BROADCAST,
    READ = BT_GATT_CHRC_READ,
    WRITE_WITHOUT_RESP = BT_GATT_CHRC_WRITE_WITHOUT_RESP,
    WRITE = BT_GATT_CHRC_WRITE,
    NOTIFY = BT_GATT_CHRC_NOTIFY,
    INDICATE = BT_GATT_CHRC_INDICATE,
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
struct ccc_store : ccc_store_base
{
    constexpr ccc_store() = default;

    template <void (*Changed)(const ::bt_gatt_attr*, ccc_flags) = nullptr,
              std::optional<att::error> (*CfgWrite)(::bt_conn*, const ::bt_gatt_attr*,
                                                    ccc_flags) = nullptr,
              bool (*CfgMatch)(::bt_conn*, const ::bt_gatt_attr*) = nullptr>
    static constexpr ccc_store create()
    {
        ccc_store store{};
        store.cfg_changed = Changed == nullptr ? nullptr
                                               : [](const ::bt_gatt_attr* attr, uint16_t flags)
        {
            return Changed(attr, static_cast<ccc_flags>(flags));
        };
        store.cfg_write = CfgWrite == nullptr
                              ? nullptr
                              : [](::bt_conn* conn, const ::bt_gatt_attr* attr, uint16_t flags)
        {
            return error_to_ssize(CfgWrite(conn, attr, static_cast<ccc_flags>(flags)),
                                  sizeof(flags));
        };
        store.cfg_match = CfgMatch;
        return store;
    }

    constexpr size_t cfg_size() const { return sizeof(cfg) / sizeof(cfg[0]); }
};

/// @brief This class stores the GATT characteristic declaration information.
class char_decl : public ::bt_gatt_chrc
{
  public:
    constexpr char_decl(const bluetooth::uuid& id, gatt::properties props, uint16_t handle = 0)
        : bt_gatt_chrc{.uuid = &id,
                       .value_handle = handle,
                       .properties =
                           static_cast<decltype(std::declval<::bt_gatt_chrc>().properties)>(props)}
    {}
};

/// @brief This class encapsulates the generic GATT attribute functionality.
struct attribute : ::bt_gatt_attr
{
    using read_fn = ssize_t (*)(::bt_conn*, const attribute*, uint8_t*, uint16_t, uint16_t);
    using write_fn = ssize_t (*)(::bt_conn*, const attribute*, const uint8_t*, uint16_t, uint16_t,
                                 write_flags);

    constexpr attribute() = default;

    template <typename T>
    constexpr attribute(const bluetooth::uuid& uuid, gatt::permissions perm,
                        ::bt_gatt_attr_read_func_t read, ::bt_gatt_attr_write_func_t write,
                        T* user_data = nullptr)
        : bt_gatt_attr{.uuid = &uuid,
                       .read = read,
                       .write = write,
                       .user_data =
                           static_cast<void*>(const_cast<std::remove_const_t<T>*>(user_data)),
                       .handle = 0,
                       .perm = static_cast<std::underlying_type_t<decltype(perm)>>(perm)}
    {}

    template <typename T>
    attribute(const bluetooth::uuid& uuid, gatt::permissions perm, read_fn read, write_fn write,
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
    constexpr attribute(const bluetooth::uuid& uuid, gatt::permissions perm, T user_value)
        requires(!std::is_pointer_v<T>)
        : bt_gatt_attr{.uuid = &uuid,
                       .read = &attribute::read_value<T>,
                       .write = nullptr,
                       .user_data =
                           std::bit_cast<void*>(static_cast<sized_unsigned_t<sizeof(void*)>>(
                               std::bit_cast<sized_unsigned_t<sizeof(T)>>(user_value))),
                       .handle = 0,
                       .perm = static_cast<std::underlying_type_t<decltype(perm)>>(perm)}
    {}

    constexpr attribute(const bluetooth::uuid& uuid, gatt::permissions perm, const char* user_value)
        : bt_gatt_attr{.uuid = &uuid,
                       .read = &attribute::read_string,
                       .write = nullptr,
                       .user_data = static_cast<void*>(const_cast<char*>(user_value)),
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
    static ssize_t read_value(::bt_conn* conn, const ::bt_gatt_attr* attr, void* buf, uint16_t len,
                              uint16_t offset)
    {
        auto value = static_cast<const attribute*>(attr)->user_value<T>();
        return bt_gatt_attr_read(conn, attr, buf, len, offset, &value, sizeof(T));
    }

    template <typename T>
    static ssize_t read_range(::bt_conn* conn, const ::bt_gatt_attr* attr, void* buf, uint16_t len,
                              uint16_t offset)
    {
        auto* range = static_cast<T*>(attr->user_data);
        return bt_gatt_attr_read(conn, attr, static_cast<void*>(buf), len, offset, range->data(),
                                 range->size());
    }

    template <size_t SIZE>
    static ssize_t read_static_data(::bt_conn* conn, const ::bt_gatt_attr* attr, void* buf,
                                    uint16_t len, uint16_t offset)
    {
        return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data, SIZE);
    }

    static ssize_t read_string(::bt_conn* conn, const ::bt_gatt_attr* attr, void* buf, uint16_t len,
                               uint16_t offset)
    {
        if (attr->user_data == nullptr)
        {
            return 0;
        }
        return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
                                 ::strlen(static_cast<const char*>(attr->user_data)));
    }

    bool ccc_active(::bt_conn* conn, ccc_flags flag) const
    {
        return bt_gatt_is_subscribed(conn, this, static_cast<uint16_t>(flag));
    }

    template <typename T>
    int notify(const std::span<const uint8_t>& data, ::bt_gatt_complete_func_t cb,
               T* user_data = nullptr, ::bt_conn* conn = nullptr) const
    {
        ::bt_gatt_notify_params params{
            .attr = this,
            .data = static_cast<const void*>(data.data()),
            .len = static_cast<uint16_t>(data.size()),
            .func = cb,
            .user_data = static_cast<void*>(const_cast<std::remove_const_t<T>*>(user_data))};
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
        explicit constexpr builder(gatt::attribute* attr)
            : attr_(attr)
        {}

        constexpr gatt::attribute* data() { return attr_; }

        /// @brief Creates a primary service attribute.
        /// @param uuid service type UUID
        /// @return builder to continue setting the attribute array
        [[nodiscard]] C2USB_STATIC_CONSTEXPR builder primary_service(const bluetooth::uuid& uuid)
        {
            *attr_ = gatt::attribute(uuid16<BT_UUID_GATT_PRIMARY_VAL>(), permissions::READ,
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
        [[nodiscard]] C2USB_STATIC_CONSTEXPR builder characteristic(const char_decl& info,
                                                                    permissions perm, TRead read,
                                                                    TWrite write,
                                                                    T* user_data = nullptr)
        {
            *attr_ = gatt::attribute(uuid16<BT_UUID_GATT_CHRC_VAL>(), permissions::READ,
                                     bt_gatt_attr_read_chrc, nullptr, &info);
            *(attr_ + 1) = gatt::attribute(*info.uuid, perm, read, write, user_data);
            return builder(attr_ + 2);
        }

        /// @brief Creates a characteristic with a fixed value, using two attributes.
        /// @tparam T the fixed value's deduced type
        /// @param info characteristic declaration
        /// @param perm characteristic value attribute access permissions
        /// @param value fixed value
        /// @return builder to continue setting the attribute array
        template <typename T>
        [[nodiscard]] C2USB_STATIC_CONSTEXPR builder characteristic(const char_decl& info,
                                                                    permissions perm, T value)
        {
            *attr_ = gatt::attribute(uuid16<BT_UUID_GATT_CHRC_VAL>(), permissions::READ,
                                     bt_gatt_attr_read_chrc, nullptr, &info);
            *(attr_ + 1) = gatt::attribute(*info.uuid, perm, value);
            return builder(attr_ + 2);
        }

        /// @brief Creates a descriptor with a fixed value.
        /// @tparam T the fixed value's deduced type
        /// @param uuid descriptor type UUID
        /// @param perm descriptor attribute access permissions
        /// @param value fixed value
        /// @return builder to continue setting the attribute array
        template <typename T>
        [[nodiscard]] C2USB_STATIC_CONSTEXPR builder descriptor(const bluetooth::uuid& uuid,
                                                                permissions perm, T value)
        {
            *attr_ = gatt::attribute(uuid, perm, value);
            return builder(attr_ + 1);
        }

        template <typename T>
        [[nodiscard]] C2USB_STATIC_CONSTEXPR builder descriptor(const bluetooth::uuid& uuid,
                                                                permissions perm,
                                                                ::bt_gatt_attr_read_func_t read,
                                                                ::bt_gatt_attr_write_func_t write,
                                                                T* user_data = nullptr)
        {
            *attr_ = gatt::attribute(uuid, perm, read, write, user_data);
            return builder(attr_ + 1);
        }

        /// @brief Creates a Client Characteristic Configuration descriptor.
        /// @param ccc the CCC context object
        /// @param perm descriptor attribute access permissions
        /// @return builder to continue setting the attribute array
        [[nodiscard]] C2USB_STATIC_CONSTEXPR builder ccc_descriptor(ccc_store& ccc,
                                                                    permissions perm)
        {
            *attr_ = gatt::attribute(uuid16<BT_UUID_GATT_CCC_VAL>(), perm, bt_gatt_attr_read_ccc,
                                     bt_gatt_attr_write_ccc, &ccc);
            return builder(attr_ + 1);
        }

      protected:
        gatt::attribute* attr_;
    };
};

/// @brief This class manages the registration of GATT services.
///        Requires the CONFIG_BT_GATT_DYNAMIC_DB to be set.
class optional_service : public ::bt_gatt_service
{
#if CONFIG_BT_GATT_DYNAMIC_DB
    bool registered_{};
#endif

  public:
    constexpr optional_service(const std::span<gatt::attribute>& attrs)
        : bt_gatt_service{.attrs = attrs.data(), .attr_count = attrs.size()}
    {}
#if CONFIG_BT_GATT_DYNAMIC_DB
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
    ~optional_service() { stop(); }
#else
    bool active() const { return true; }
#endif
};

} // namespace bluetooth::gatt

template <>
struct magic_enum::customize::enum_range<bluetooth::gatt::permissions>
{
    static constexpr bool is_flags = true;
};
template <>
struct magic_enum::customize::enum_range<bluetooth::gatt::properties>
{
    static constexpr bool is_flags = true;
};
template <>
struct magic_enum::customize::enum_range<bluetooth::gatt::write_flags>
{
    static constexpr bool is_flags = true;
};
template <>
struct magic_enum::customize::enum_range<bluetooth::gatt::ccc_flags>
{
    static constexpr bool is_flags = true;
};
