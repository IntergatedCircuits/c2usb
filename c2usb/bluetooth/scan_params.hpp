// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "bluetooth/base.hpp"
#include "bluetooth/gatt.hpp"

namespace bluetooth::scan_params
{
struct scan_interval_window
{
    c2usb::le_uint16_t scan_interval;
    c2usb::le_uint16_t scan_window;
};

/// @brief  GATT service for Bluetooth scan parameters, supporting a single central.
///         Use clear() to reset the scan parameters on a new connection.
class service
{
  private:
    static const gatt::char_decl& scan_interval_window_info()
    {
        static const gatt::char_decl info{uuid16<BT_UUID_GATT_SIW_VAL>(),
                                          gatt::properties::WRITE_WITHOUT_RESP};
        return info;
    }
    static const gatt::char_decl& scan_refresh_info()
    {
        static const gatt::char_decl info{uuid16<BT_UUID_GATT_SR_VAL>(), gatt::properties::NOTIFY};
        return info;
    }

    static ssize_t write_scan_interval_window(::bt_conn* conn, const ::bt_gatt_attr* attr,
                                              void const* buf, uint16_t len, uint16_t offset,
                                              uint8_t flags)
    {
        auto* self = static_cast<service*>(attr->user_data);
        if ((offset + len) > sizeof(scan_interval_window))
        {
            return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
        }
        std::copy_n(static_cast<const uint8_t*>(buf), len,
                    reinterpret_cast<uint8_t*>(&self->params_) + offset);
        return len;
    }

    static constexpr size_t attribute_count = 1 + 2;

    std::array<gatt::attribute, attribute_count> attributes_;
    scan_interval_window params_{};

  public:
    constexpr std::span<const gatt::attribute> attributes() const { return attributes_; }

    explicit service(security level)
    {
        auto attr_tail = gatt::attribute::builder(attributes_.data())
                             .primary_service(uuid16<BT_UUID_SPS_VAL>())
                             .characteristic(scan_interval_window_info(), gatt::writeable_at(level),
                                             nullptr, &write_scan_interval_window, this);
        assert(attr_tail.data() == (attributes_.data() + attributes_.size()));
    }

    void clear() { params_ = {}; }

    [[nodiscard]] std::optional<scan_interval_window> get() const
    {
        if (std::bit_cast<uint32_t>(params_) == 0)
        {
            return std::nullopt;
        }
        return params_;
    }

    [[nodiscard]] std::optional<unsigned> duty_cycle() const
    {
        if (params_.scan_interval == 0)
        {
            return std::nullopt;
        }
        return unsigned(params_.scan_window) * 100 / unsigned(params_.scan_interval);
    }

    // to be assigned as:
    // static const STRUCT_SECTION_ITERABLE(bt_gatt_service_static, dis_svc) =
    [[nodiscard]] constexpr ::bt_gatt_service_static static_service() const
    {
        return ::bt_gatt_service_static{.attrs = attributes_.data(),
                                        .attr_count = attributes_.size()};
    }
    // only use either static_service() or dynamic_service(), not both
#if CONFIG_BT_GATT_DYNAMIC_DB
    // can be enabled or disabled at runtime, but preferably started before bt_enable()
    [[nodiscard]] constexpr auto dynamic_service() const
    {
        return bluetooth::gatt::optional_service{attributes()};
    }
#endif
};

} // namespace bluetooth::scan_params
