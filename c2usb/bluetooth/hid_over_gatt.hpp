// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "bluetooth/gatt.hpp"
#include "hid/transport.hpp"
#include "usb/base.hpp"
#include "usb/class/hid.hpp"
#include <etl/delegate.h>
#include <hid/report.hpp>
#include <hid/report_protocol.hpp>

namespace bluetooth::hid_over_gatt
{
/// @brief  HID over GATT feature flags.
enum class flags : uint8_t
{
    NONE = 0,
    REMOTE_WAKE = 1, // whether HID Device is capable of sending a wake-signal to a HID Host
    NORMALLY_CONNECTABLE =
        2, // whether HID Device will be advertising when bonded but not connected
};

/// @brief  HID over GATT general descriptor.
struct info
{
    usb::version bcdHID{usb::hid::SPEC_VERSION};
    usb::hid::country_code bCountryCode{};
    flags bFlags{};

    info(flags flag = {}, usb::hid::country_code country_code = {})
        : bCountryCode(country_code), bFlags(flag)
    {}
};

enum class event : uint8_t
{
    SUSPEND = 0,
    EXIT_SUSPEND = 1,
};

struct session_params : public ::hid::session::params
{
    ::bt_conn* conn{};
};

/// @brief  This class implements the HID over GATT protocol service.
///         To instantiate an object of this class, see @ref service_instance below.
class service : public hid::transport
{
  public:
    c2usb::result send_report(hid::session& sess, const std::span<const uint8_t>& data) override;
    c2usb::result receive_report(hid::session& sess, const std::span<uint8_t>& data,
                                 hid::report::type type = hid::report::type::OUTPUT) override;

    using power_event_delegate = etl::delegate<void(service&, event)>;
#if CONFIG_C2USB_HOGP_POWER_EVENT
    void set_power_event_delegate(const power_event_delegate& delegate)
    {
        power_event_delegate_ = delegate;
    }
#endif

  protected:
    struct ccc_data : public gatt::ccc_store
    {
        std::span<const uint8_t> pending_notify{};
        ccc_data() = default;
    };

  private:
    template <typename T, void (service::*FUNC)(T)>
    static void for_each(T data)
    {
        bt_gatt_foreach_attr_type(
            std::numeric_limits<uint16_t>::min(), std::numeric_limits<uint16_t>::max(),
            control_point_info().uuid, // this attribute is unique, and uses this as user_data
            nullptr, 0,
            [](const ::bt_gatt_attr* attr, uint16_t, void* user_data)
            {
                auto* self = static_cast<service*>(attr->user_data);
                (self->*FUNC)(static_cast<T>(user_data));
                return (uint8_t)BT_GATT_ITER_CONTINUE;
            },
            static_cast<void*>(data));
    }

    gatt::permissions access() const { return access_; }
    gatt::permissions read_access() const
    {
        using namespace bluetooth::gatt;
        using namespace magic_enum::bitwise_operators;
        return access() & (permissions::READ | permissions::READ_AUTHEN |
                           permissions::READ_ENCRYPT | permissions::READ_LESC);
    }
    gatt::permissions write_access() const
    {
        using namespace bluetooth::gatt;
        using namespace magic_enum::bitwise_operators;
        return access() & (permissions::WRITE | permissions::WRITE_AUTHEN |
                           permissions::WRITE_ENCRYPT | permissions::WRITE_LESC);
    }

    static const gatt::char_decl& report_map_info()
    {
        using namespace bluetooth::gatt;
        static const char_decl info{uuid16<BT_UUID_HIDS_REPORT_MAP_VAL>(), properties::READ};
        return info;
    }
    static const gatt::char_decl& hid_info()
    {
        using namespace bluetooth::gatt;
        static const char_decl info{uuid16<BT_UUID_HIDS_INFO_VAL>(), properties::READ};
        return info;
    }
    static const gatt::char_decl& protocol_mode_info()
    {
        using namespace bluetooth::gatt;
        using namespace magic_enum::bitwise_operators;
        static const char_decl info{uuid16<BT_UUID_HIDS_PROTOCOL_MODE_VAL>(),
                                    properties::READ | properties::WRITE_WITHOUT_RESP};
        return info;
    }
    static const gatt::char_decl& control_point_info()
    {
        using namespace bluetooth::gatt;
        using namespace magic_enum::bitwise_operators;
        static const char_decl info{uuid16<BT_UUID_HIDS_CTRL_POINT_VAL>(),
                                    properties::WRITE_WITHOUT_RESP};
        return info;
    }
    static const gatt::char_decl& input_report_info()
    {
        using namespace bluetooth::gatt;
        using namespace magic_enum::bitwise_operators;
        static const char_decl info{uuid16<BT_UUID_HIDS_REPORT_VAL>(),
                                    properties::READ | properties::NOTIFY};
        return info;
    }
    static const gatt::char_decl& output_report_info()
    {
        using namespace bluetooth::gatt;
        using namespace magic_enum::bitwise_operators;
        static const char_decl info{uuid16<BT_UUID_HIDS_REPORT_VAL>(),
                                    properties::READ | properties::WRITE |
                                        properties::WRITE_WITHOUT_RESP};
        return info;
    }
    static const gatt::char_decl& feature_report_info() { return output_report_info(); }
    static const gatt::char_decl& boot_keyboard_in_info()
    {
        using namespace bluetooth::gatt;
        using namespace magic_enum::bitwise_operators;
        static const char_decl info{uuid16<BT_UUID_HIDS_BOOT_KB_IN_REPORT_VAL>(),
                                    properties::READ | properties::NOTIFY};
        return info;
    }
    static const gatt::char_decl& boot_keyboard_out_info()
    {
        using namespace bluetooth::gatt;
        using namespace magic_enum::bitwise_operators;
        static const char_decl info{uuid16<BT_UUID_HIDS_BOOT_KB_OUT_REPORT_VAL>(),
                                    properties::READ | properties::WRITE |
                                        properties::WRITE_WITHOUT_RESP};
        return info;
    }
    static const gatt::char_decl& boot_mouse_in_info()
    {
        using namespace bluetooth::gatt;
        using namespace magic_enum::bitwise_operators;
        static const char_decl info{uuid16<BT_UUID_HIDS_BOOT_MOUSE_IN_REPORT_VAL>(),
                                    properties::READ | properties::NOTIFY};
        return info;
    }

    std::span<const gatt::attribute> attributes() const
    {
        return {static_cast<const gatt::attribute*>(gatt_service_.attrs), gatt_service_.attr_count};
    }

    std::span<const uint8_t>& get_pending_notify(const gatt::attribute* attr);

    static ssize_t get_report_map(::bt_conn* conn, const ::bt_gatt_attr* attr, void* buf,
                                  uint16_t len, uint16_t offset);

    static ssize_t get_protocol_mode(::bt_conn* conn, const ::bt_gatt_attr* attr, void* buf,
                                     uint16_t len, uint16_t offset);

    static ssize_t set_protocol_mode(::bt_conn* conn, const ::bt_gatt_attr* attr, void const* buf,
                                     uint16_t len, uint16_t offset, uint8_t flags);

    static ssize_t control_point_request(::bt_conn* conn, const ::bt_gatt_attr* attr,
                                         void const* buf, uint16_t len, uint16_t offset,
                                         uint8_t flags);

    static ssize_t get_report(::bt_conn* conn, const gatt::attribute* attr, uint8_t* buf,
                              uint16_t len, uint16_t offset);

    static ssize_t set_report(::bt_conn* conn, const gatt::attribute* attr, const uint8_t* buf,
                              uint16_t len, uint16_t offset, gatt::write_flags flag);

    static ssize_t get_boot_report(::bt_conn* conn, const gatt::attribute* attr, uint8_t* buf,
                                   uint16_t len, uint16_t offset);

    static ssize_t set_boot_report(::bt_conn* conn, const gatt::attribute* attr, const uint8_t* buf,
                                   uint16_t len, uint16_t offset, gatt::write_flags flag);

    static ssize_t ccc_cfg_write(::bt_conn* conn, const gatt::attribute* attr,
                                 gatt::ccc_flags flags);

    static hid::report::properties report_attr_props(const gatt::attribute* attr);

    gatt::attribute::builder add_input_report(gatt::attribute::builder attr_tail, ccc_data* ccc,
                                              hid::report::properties props)
    {
        ccc->cfg_write = [](::bt_conn* conn, const ::bt_gatt_attr* attr, uint16_t flags)
        {
            return ccc_cfg_write(conn, static_cast<const gatt::attribute*>(attr),
                                 static_cast<gatt::ccc_flags>(flags));
        };

        return attr_tail
            .characteristic(input_report_info(), read_access(), &service::get_report, nullptr, this)
            .descriptor(uuid16<BT_UUID_HIDS_REPORT_REF_VAL>(), read_access(),
                        &gatt::attribute::read_value<hid::report::selector>, nullptr,
                        std::bit_cast<void*>(props))
            .ccc_descriptor(*ccc, access());
    }
    gatt::attribute::builder add_output_report(gatt::attribute::builder attr_tail,
                                               hid::report::properties props)
    {
        return attr_tail
            .characteristic(output_report_info(), access(), &service::get_report,
                            &service::set_report, this)
            .descriptor(uuid16<BT_UUID_HIDS_REPORT_REF_VAL>(), read_access(),
                        &gatt::attribute::read_value<hid::report::selector>, nullptr,
                        std::bit_cast<void*>(props));
    }
    gatt::attribute::builder add_feature_report(gatt::attribute::builder attr_tail,
                                                hid::report::properties props)
    {
        return attr_tail
            .characteristic(feature_report_info(), access(), &service::get_report,
                            &service::set_report, this)
            .descriptor(uuid16<BT_UUID_HIDS_REPORT_REF_VAL>(), read_access(),
                        &gatt::attribute::read_value<hid::report::selector>, nullptr,
                        std::bit_cast<void*>(props));
    }

    const gatt::attribute* input_report_attr(::hid::report::id::type id) const;
    const gatt::attribute* input_boot_attr(hid::boot::mode bm) const;

    void notify_callback(::bt_conn* conn, const gatt::attribute* attr);

    size_t report_data_offset() const
    {
        // report ID is not included in report characteristic data exchange
        return app_.report_info().uses_report_ids() ? sizeof(::hid::report::id) : 0;
    }

    struct conn_session
    {
        ::bt_conn* conn{};
        hid::session* session{};
        hid::transport::reports_receiver rx_buffers_{};
    };
    conn_session& make_session(::bt_conn* conn, hid::protocol prot);
    void disconnected(::bt_conn* conn);

    ssize_t get_report_data(hid::session& sess, hid::report::properties props, uint8_t* buf,
                            uint16_t len, uint16_t offset);
    ssize_t set_report_data(conn_session& conns, hid::report::properties props, const uint8_t* buf,
                            uint16_t len, uint16_t offset);

    hid::application& app_;
    std::array<conn_session, CONFIG_BT_MAX_CONN> sessions_{};
    gatt::optional_service gatt_service_;
#if CONFIG_C2USB_HOGP_POWER_EVENT
    power_event_delegate power_event_delegate_{};
#endif
    const gatt::permissions access_;
#if CONFIG_C2USB_HID_BOOT_PROTOCOL
    const hid::boot::mode boot_mode_;
    auto boot_mode() const { return boot_mode_; }
#else
    auto boot_mode() const { return hid::boot::mode::NONE; }
#endif

  protected:
    std::span<gatt::attribute>
    fill_attributes(const std::span<const hid::report::properties>& report_table,
                    const std::span<gatt::attribute>& attrs, const std::span<ccc_data>& cccs,
                    flags f)
    {
        auto* ccc_ptr = cccs.data();

        // base service attributes
        auto attr_tail =
            gatt::attribute::builder(attrs.data())
                .primary_service(uuid16<BT_UUID_HIDS_VAL>())
#if 1
                .characteristic(
                    report_map_info(), read_access(),
                    &gatt::attribute::read_range<hid::report_protocol::descriptor_view_type>,
                    nullptr, &app_.report_info().descriptor)
#else
                .characteristic(report_map_info(), read_access(), &service::get_report_map, nullptr,
                                this)
#endif
                .characteristic(hid_info(), read_access(), hid_over_gatt::info(f))
                .characteristic(control_point_info(), write_access(), nullptr,
                                &service::control_point_request, this);

        assert(attr_tail.data() == (attrs.data() + base_attribute_count()));

        // report protocol attributes
        for (auto props : report_table)
        {
            switch (props.selector.type())
            {
            case hid::report::type::INPUT:
                attr_tail = add_input_report(attr_tail, ccc_ptr, props);
                ccc_ptr++;
                break;
            case hid::report::type::OUTPUT:
                attr_tail = add_output_report(attr_tail, props);
                break;
            case hid::report::type::FEATURE:
                attr_tail = add_feature_report(attr_tail, props);
                break;
            default:
                assert(false);
                break;
            }
        }

        // boot protocol attributes
        if (IS_ENABLED(CONFIG_C2USB_HID_BOOT_PROTOCOL) and (boot_mode() != hid::boot::mode::NONE))
        {
            attr_tail = attr_tail.characteristic(protocol_mode_info(), access(),
                                                 &service::get_protocol_mode,
                                                 &service::set_protocol_mode, this);

            if ((uint8_t(boot_mode()) & uint8_t(hid::boot::mode::KEYBOARD)) != 0)
            {
                ccc_ptr->cfg_write = [](::bt_conn* conn, const ::bt_gatt_attr* attr, uint16_t flags)
                {
                    return ccc_cfg_write(conn, static_cast<const gatt::attribute*>(attr),
                                         static_cast<gatt::ccc_flags>(flags));
                };
                attr_tail =
                    attr_tail
                        .characteristic(boot_keyboard_out_info(), access(),
                                        &service::get_boot_report, &service::set_boot_report, this)
                        .characteristic(boot_keyboard_in_info(), read_access(),
                                        &service::get_boot_report, nullptr, this)
                        .ccc_descriptor(*ccc_ptr, access());
                ccc_ptr++;
            }
            if ((uint8_t(boot_mode()) & uint8_t(hid::boot::mode::MOUSE)) != 0)
            {
                ccc_ptr->cfg_write = [](::bt_conn* conn, const ::bt_gatt_attr* attr, uint16_t flags)
                {
                    return ccc_cfg_write(conn, static_cast<const gatt::attribute*>(attr),
                                         static_cast<gatt::ccc_flags>(flags));
                };
                attr_tail = attr_tail
                                .characteristic(boot_mouse_in_info(), read_access(),
                                                &service::get_boot_report, nullptr, this)
                                .ccc_descriptor(*ccc_ptr, access());
                ccc_ptr++;
            }
        }
        assert(attr_tail.data() == (attrs.data() + attrs.size()));
        assert(ccc_ptr == (cccs.data() + cccs.size()));

        return attrs;
    }

    constexpr service(hid::application& app, security level,
                      const std::span<gatt::attribute>& attrs,
                      auto boot_mode = hid::boot::mode::NONE)
        : app_(app),
          gatt_service_(attrs),
          access_(gatt::readwriteable_at(level))
#if CONFIG_C2USB_HID_BOOT_PROTOCOL
          ,
          boot_mode_(boot_mode)
#endif
    {}

    static constexpr size_t base_attribute_count()
    {
        return 1   // service
               + 2 // report map
               + 2 // HID info
               + 2 // control point
            ;
    }
    static constexpr size_t boot_attribute_count(hid::report::type type)
    {
        return 2                                     // report characteristic
               + (type == hid::report::type::INPUT); // CCC
    }
    static constexpr size_t report_attribute_count(hid::report::type type)
    {
        return boot_attribute_count(type) + 1; // report reference
    }
    static constexpr size_t report_reference_offset() { return 2; }
    static constexpr size_t boot_mode_attribute_count(hid::boot::mode boot)
    {
        if (boot ==
            hid::boot::mode(uint8_t(hid::boot::mode::KEYBOARD) | uint8_t(hid::boot::mode::MOUSE)))
        {
            return 2 + boot_attribute_count(hid::report::type::INPUT) * 2 +
                   boot_attribute_count(hid::report::type::OUTPUT) * 2;
        }
        switch (boot)
        {
        case hid::boot::mode::KEYBOARD:
            return 2 + boot_attribute_count(hid::report::type::INPUT) +
                   boot_attribute_count(hid::report::type::OUTPUT);
        case hid::boot::mode::MOUSE:
            return 2 + boot_attribute_count(hid::report::type::INPUT);
        default:
            return 0;
        }
    }
    static constexpr size_t attribute_count(const ::hid::report_protocol_properties& props,
                                            auto boot = hid::boot::mode::NONE)
    {
        size_t size = base_attribute_count();
        size += report_attribute_count(hid::report::type::INPUT) * props.input_report_count;
        size += report_attribute_count(hid::report::type::OUTPUT) * props.output_report_count;
        size += report_attribute_count(hid::report::type::FEATURE) * props.feature_report_count;
        size += boot_mode_attribute_count(boot);
        return size;
    }
    static constexpr size_t ccc_count(const hid::report_protocol_properties& props,
                                      auto boot = hid::boot::mode::NONE)
    {
        return props.input_report_count + (boot != hid::boot::mode::NONE);
    }

  public:
    ::bt_conn* get_session_conn(const hid::session& sess) const
    {
        auto it = std::ranges::find_if(sessions_,
                                       [&](const conn_session& s) { return s.session == &sess; });
        return (it != sessions_.end()) ? it->conn : nullptr;
    }
    hid::session* get_session(::bt_conn* conn) const
    {
        auto it =
            std::ranges::find_if(sessions_, [&](const conn_session& s) { return s.conn == conn; });
        return (it != sessions_.end()) ? it->session : nullptr;
    }
    auto session_count() const
    {
        return std::ranges::count_if(sessions_,
                                     [](const conn_session& s) { return s.session != nullptr; });
    }

    static void disconnect_callback(::bt_conn* conn, uint8_t reason);
};

/// @brief  This class creates the HID over GATT service with the necessary storage.
/// @tparam REPORT_DESCRIPTOR the HID report descriptor as a byte array
///@tparam  BOOT the boot protocol mode to support (if any)
template <auto REPORT_DESCRIPTOR
#if CONFIG_C2USB_HID_BOOT_PROTOCOL
          ,
          auto BOOT = hid::boot::mode::NONE
#endif
          >
class service_instance : public service
{
    static inline constexpr ::hid::report_protocol_properties REPORT_PROPS{
        ::hid::rdf::ce_descriptor_view{REPORT_DESCRIPTOR}};
    static inline constexpr auto REPORT_TABLE =
        ::hid::make_report_properties_table<REPORT_DESCRIPTOR>();

#ifndef CONFIG_C2USB_HID_BOOT_PROTOCOL
    static constexpr auto BOOT = hid::boot::mode::NONE;
#endif
    std::array<gatt::attribute, attribute_count(REPORT_PROPS, BOOT)> attributes_;
    std::array<ccc_data, ccc_count(REPORT_PROPS, BOOT)> ccc_stores_;

  public:
    constexpr service_instance(hid::application& app, security level,
                               flags f = (flags)((uint8_t)flags::REMOTE_WAKE |
                                                 (uint8_t)flags::NORMALLY_CONNECTABLE))
        : service(app, level, attributes_, BOOT)
    {
        fill_attributes(REPORT_TABLE, attributes_, ccc_stores_, f);
    }

    constexpr std::span<const gatt::attribute> attributes() const { return {attributes_}; }

    // to be assigned as:
    // static const STRUCT_SECTION_ITERABLE(bt_gatt_service_static, hop_svc) =
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

} // namespace bluetooth::hid_over_gatt

template <>
struct magic_enum::customize::enum_range<bluetooth::hid_over_gatt::flags>
{
    static constexpr bool is_flags = true;
};
