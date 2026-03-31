// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "bluetooth/gatt.hpp"
#include "c2usb.hpp"
#include "raw_to_hex_string.hpp"
#include "usb/product_info.hpp"
#include <make_static.hpp>

namespace bluetooth::device_info
{
enum class vendor_id_source : uint8_t
{
    BLUETOOTH_SIG = 1,
    USB_IF = 2,
    UNKNOWN = 255,
};

struct pnp_id
{
    vendor_id_source vid_source;
    c2usb::le_uint16_t vendor_id;
    c2usb::le_uint16_t product_id;
    c2usb::le_uint16_t product_version;
};

struct revisions
{
    const char* hw;
    const char* fw;
    const char* sw;
};

class service
{
  private:
    static const gatt::char_decl& manufacturer_name_info()
    {
        static const gatt::char_decl info{uuid16<BT_UUID_DIS_MANUFACTURER_NAME_VAL>(),
                                          gatt::properties::READ};
        return info;
    }
    static const gatt::char_decl& model_number_info()
    {
        static const gatt::char_decl info{uuid16<BT_UUID_DIS_MODEL_NUMBER_VAL>(),
                                          gatt::properties::READ};
        return info;
    }
    static const gatt::char_decl& serial_number_info()
    {
        static const gatt::char_decl info{uuid16<BT_UUID_DIS_SERIAL_NUMBER_VAL>(),
                                          gatt::properties::READ};
        return info;
    }
    static const gatt::char_decl& hw_revision_info()
    {
        static const gatt::char_decl info{uuid16<BT_UUID_DIS_HARDWARE_REVISION_VAL>(),
                                          gatt::properties::READ};
        return info;
    }
    static const gatt::char_decl& fw_revision_info()
    {
        static const gatt::char_decl info{uuid16<BT_UUID_DIS_FIRMWARE_REVISION_VAL>(),
                                          gatt::properties::READ};
        return info;
    }
    static const gatt::char_decl& sw_revision_info()
    {
        static const gatt::char_decl info{uuid16<BT_UUID_DIS_SOFTWARE_REVISION_VAL>(),
                                          gatt::properties::READ};
        return info;
    }
    static const gatt::char_decl& pnp_id_info()
    {
        static const gatt::char_decl info{uuid16<BT_UUID_DIS_PNP_ID_VAL>(), gatt::properties::READ};
        return info;
    }

    static ssize_t read_serial_number(::bt_conn* conn, const ::bt_gatt_attr* attr, void* buf,
                                      uint16_t len, uint16_t offset)
    {
        auto sn = static_cast<const usb::product_info*>(attr->user_data)->serial_number();
        if (auto* serial_str = std::get_if<std::string_view>(&sn))
        {
            return bt_gatt_attr_read(conn, attr, buf, len, offset,
                                     reinterpret_cast<const uint8_t*>(serial_str->data()),
                                     serial_str->size());
        }
        if (auto* serial_span = std::get_if<std::span<const uint8_t>>(&sn))
        {
            if (offset > (serial_span->size() * 2))
            {
                return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
            }
            // or use std::to_chars
            return c2usb::raw_to_hex_string(serial_span->subspan(offset / 2),
                                            std::span<char>(static_cast<char*>(buf), len));
        }
        return 0;
    }

    template <vendor_id_source VID_SRC>
    static ssize_t read_pnp_id(::bt_conn* conn, const ::bt_gatt_attr* attr, void* buf, uint16_t len,
                               uint16_t offset)
    {
        auto* info = static_cast<const usb::product_info*>(attr->user_data);
        pnp_id pnpid{.vid_source = VID_SRC,
                     .vendor_id = info->vendor_id,
                     .product_id = info->product_id,
                     .product_version = info->product_version};
        return bt_gatt_attr_read(conn, attr, buf, len, offset, reinterpret_cast<uint8_t*>(&pnpid),
                                 sizeof(pnpid));
    }

  protected:
    template <vendor_id_source VID_SRC, bool SN>
    std::span<gatt::attribute> fill_attributes(const usb::product_info& info, const revisions& revs,
                                               const std::span<gatt::attribute>& attrs,
                                               gatt::permissions perm)
    {
        auto attr_tail =
            gatt::attribute::builder(attrs.data()).primary_service(uuid16<BT_UUID_DIS_VAL>());

        {
            attr_tail = attr_tail.characteristic(manufacturer_name_info(), perm, info.vendor_name);
        }
        {
            attr_tail = attr_tail.characteristic(model_number_info(), perm, info.product_name);
        }
        if (revs.hw)
        {
            attr_tail = attr_tail.characteristic(hw_revision_info(), perm, revs.hw);
        }
        if (revs.fw)
        {
            attr_tail = attr_tail.characteristic(fw_revision_info(), perm, revs.fw);
        }
        if (revs.sw)
        {
            attr_tail = attr_tail.characteristic(sw_revision_info(), perm, revs.sw);
        }
        if (VID_SRC != vendor_id_source::UNKNOWN)
        {
            attr_tail = attr_tail.characteristic(pnp_id_info(), perm, &read_pnp_id<VID_SRC>,
                                                 nullptr, &info);
        }
        if (SN)
        {
            attr_tail = attr_tail.characteristic(serial_number_info(), perm, &read_serial_number,
                                                 nullptr, &info);
        }
        assert(attr_tail.data() <= (attrs.data() + attrs.size()));
        return std::span<gatt::attribute>{attrs.data(), attr_tail.data()};
    }

    constexpr service() = default;

    static constexpr size_t attribute_count(const revisions& revs, vendor_id_source vid_src,
                                            bool sn)
    {
        constexpr size_t char_attr_count = 2;
        size_t char_count = 2; // vendor name and product name
        char_count += bool(revs.hw) + bool(revs.fw) + bool(revs.sw) +
                      bool(vid_src != vendor_id_source::UNKNOWN) + sn;
        return 1 + char_count * char_attr_count; // +1 for the primary service attribute
    }
};

template <revisions REVS, vendor_id_source VID_SRC = vendor_id_source::UNKNOWN, bool SN = true>
class service_instance : public service
{
    std::array<gatt::attribute, attribute_count(make_static<REVS>(), VID_SRC, SN)> attributes_;

  public:
    constexpr service_instance(const usb::product_info& info, security level = security::L1_NONE)
        : service()
    {
        fill_attributes<VID_SRC, SN>(info, make_static<REVS>(), attributes_,
                                     gatt::readable_at(level));
    }

    constexpr std::span<const gatt::attribute> attributes() const { return attributes_; }

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

} // namespace bluetooth::device_info
