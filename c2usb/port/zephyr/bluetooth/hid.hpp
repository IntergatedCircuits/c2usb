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
#ifndef __PORT_ZEPHYR_BLUETOOTH_HID_HPP_
#define __PORT_ZEPHYR_BLUETOOTH_HID_HPP_

#include "port/zephyr/bluetooth/gatt.hpp"
#if C2USB_HAS_ZEPHYR_BT_GATT_HEADERS

#include <atomic>
#include <etl/delegate.h>

#include "hid/application.hpp"
#include "hid/report_protocol.hpp"
#include "uninit_store.hpp"
#include "usb/class/hid.hpp"

namespace bluetooth::zephyr::hid
{
/// @brief  HID over GATT feature flags.
enum class flags : uint8_t
{
    NONE = 0,
    REMOTE_WAKE = 1,
    NORMALLY_CONNECTABLE = 2,
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

/// @brief  HID over GATT power event.
enum class event : uint8_t
{
    SUSPEND = 0,
    EXIT_SUSPEND = 1,
};

/// @brief  HID over GATT security access control levels.
enum class security : uint8_t
{
    NONE = 0,
    ENCRYPT = 1,
    AUTH_ENCRYPT = 2, // encryption using authenticated link-key
};

using boot_protocol_mode = usb::hid::boot_protocol_mode;

/// @brief  This class implements the HID over GATT protocol service.
///         To instantiate an object of this class, see @ref service_instance below.
class service : public ::hid::transport
{
  public:
    using power_event_delegate = etl::delegate<void(service&, event)>;

    bool start() { return gatt_service_.start(); }
    bool stop() { return gatt_service_.stop(); }
    bool active() const { return gatt_service_.active(); }

    void set_power_event_delegate(const power_event_delegate& delegate)
    {
        power_event_delegate_ = delegate;
    }

    template <typename T, void (service::*FUNC)(T)>
    static void for_each(T data)
    {
        bt_gatt_foreach_attr_type(
            std::numeric_limits<uint16_t>::min(), std::numeric_limits<uint16_t>::max(),
            protocol_mode_info().uuid, // this attribute is unique, and uses this as user_data
            nullptr, 0,
            [](const ::bt_gatt_attr* attr, uint16_t, void* user_data)
            {
                auto* this_ = static_cast<service*>(attr->user_data);
                (this_->*FUNC)(static_cast<T>(user_data));
                return (uint8_t)BT_GATT_ITER_CONTINUE;
            },
            static_cast<void*>(data));
    }

    void connected(::bt_conn* conn);
    void disconnected(::bt_conn* conn);
    ::bt_conn* peer() const { return active_conn_.load(); }

  private:
    static const gatt::char_decl& report_map_info();
    static const gatt::char_decl& hid_info();
    static const gatt::char_decl& protocol_mode_info();
    static const gatt::char_decl& control_point_info();
    static const gatt::char_decl& input_report_info();
    static const gatt::char_decl& output_report_info();
    static const gatt::char_decl& feature_report_info();
    static const gatt::char_decl& boot_keyboard_in_info();
    static const gatt::char_decl& boot_keyboard_out_info();
    static const gatt::char_decl& boot_mouse_in_info();

    static gatt::permissions get_access(security sec);
    gatt::permissions access() const;
    gatt::permissions read_access() const;
    gatt::permissions write_access() const;

    ::hid::result send_report(const std::span<const uint8_t>& data,
                              ::hid::report::type type) override;
    ::hid::result receive_report(const std::span<uint8_t>& data, ::hid::report::type type) override;

    std::span<const uint8_t>& get_pending_notify(::hid::protocol prot, ::hid::report::id id);

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

    void request_get_report(::hid::report::selector sel, const std::span<uint8_t>& buffer);

    static ::hid::report::selector report_attr_selector(const gatt::attribute* attr)
    {
        return (attr + 1)->user_value<::hid::report::selector>();
    }

    gatt::attribute::builder add_input_report(gatt::attribute::builder attr_tail,
                                              gatt::ccc_store* ccc, ::hid::report::id::type id = 0);
    gatt::attribute::builder add_output_report(gatt::attribute::builder attr_tail,
                                               ::hid::report::id::type id = 0);
    gatt::attribute::builder add_feature_report(gatt::attribute::builder attr_tail,
                                                ::hid::report::id::type id = 0);

    ::hid::protocol get_protocol();

    bool start_app(::bt_conn* conn, ::hid::protocol protocol = ::hid::protocol::REPORT);
    void stop_app(::bt_conn* conn);

    std::span<const gatt::attribute> attributes() const
    {
        return {static_cast<const gatt::attribute*>(gatt_service_.attrs), gatt_service_.attr_count};
    }

    size_t report_data_offset() const
    {
        // report ID is not included in report characteristic data exchange
        return app_.report_info().uses_report_ids() ? sizeof(::hid::report::id) : 0;
    }

    std::span<gatt::attribute> fill_attributes(const std::span<gatt::attribute>& attrs,
                                               const std::span<gatt::ccc_store>& cccs, flags f);

    const gatt::attribute* input_report_attr(::hid::report::id::type id) const;
    const gatt::attribute* input_boot_attr() const;

    ::hid::application& app_;
    const gatt::permissions access_;
    ::hid::report::selector get_report_{};
    std::atomic<::bt_conn*> active_conn_{};
    ::hid::reports_receiver rx_buffers_{};
    std::span<const uint8_t> get_report_buffer_{};
    std::span<const uint8_t> input_buffer_{};
    power_event_delegate power_event_delegate_{};
    const boot_protocol_mode boot_mode_;
    gatt::service gatt_service_; // leave as last

  protected:
    service(::hid::application& app, flags f, security sec, const std::span<gatt::attribute>& attrs,
            const std::span<gatt::ccc_store>& cccs, auto boot_mode = boot_protocol_mode::NONE)
        : app_(app),
          access_(get_access(sec)),
          boot_mode_(boot_mode),
          gatt_service_(fill_attributes(attrs, cccs, f))
    {}

    static constexpr size_t base_attribute_count()
    {
        return 1   // service
               + 2 // report map
               + 2 // HID info
               + 2 // protocol mode
               + 2 // control point
            ;
    }
    static constexpr size_t boot_attribute_count(::hid::report::type type)
    {
        return 2                                       // report characteristic
               + (type == ::hid::report::type::INPUT); // CCC
    }
    static constexpr size_t report_attribute_count(::hid::report::type type)
    {
        return boot_attribute_count(type) + 1; // report reference
    }
    static constexpr size_t report_reference_offset() { return 2; }
    static constexpr size_t report_count(const ::hid::report_protocol_properties& props,
                                         ::hid::report::type type)
    {
        if (props.max_report_size(type) == 0)
        {
            return 0;
        }
        if (props.max_report_id(type) == 0)
        {
            return 1;
        }
        return props.max_report_id(type);
    }
    static constexpr size_t boot_mode_attribute_count(boot_protocol_mode boot)
    {
        using namespace ::hid::report;
        switch (boot)
        {
        case boot_protocol_mode::KEYBOARD:
            return boot_attribute_count(type::INPUT) + boot_attribute_count(type::OUTPUT);
        case boot_protocol_mode::MOUSE:
            return boot_attribute_count(type::INPUT);
        default:
            return 0;
        }
    }
    static constexpr size_t attribute_count(const ::hid::report_protocol_properties& props,
                                            auto boot = boot_protocol_mode::NONE)
    {
        using namespace ::hid::report;
        size_t size = base_attribute_count();
        // note that these numbers are an upper bound only,
        // attributes will be wasted if the report ID numbering is not contiguous
        size += report_attribute_count(type::INPUT) * report_count(props, type::INPUT);
        size += report_attribute_count(type::OUTPUT) * report_count(props, type::OUTPUT);
        size += report_attribute_count(type::FEATURE) * report_count(props, type::FEATURE);
        switch (boot)
        {
        case boot_protocol_mode::KEYBOARD:
            size += report_attribute_count(type::INPUT) - 1;
            size += report_attribute_count(type::OUTPUT) - 1;
            break;
        case boot_protocol_mode::MOUSE:
            size += report_attribute_count(type::INPUT) - 1;
            break;
        default:
            break;
        }
        return size;
    }
    static constexpr size_t ccc_count(const ::hid::report_protocol_properties& props,
                                      auto boot = boot_protocol_mode::NONE)
    {
        return report_count(props, ::hid::report::type::INPUT) + (boot != boot_protocol_mode::NONE);
    }
};

/// @brief  This class creates the HID over GATT service with the necessary storage.
/// @tparam REPORT_PROPS the report properties that are created out of the HID report descriptor
template <::hid::report_protocol_properties REPORT_PROPS, auto BOOT = boot_protocol_mode::NONE>
class service_instance : public service
{
    friend class service;

  public:
    service_instance(::hid::application& app, security sec,
                     flags f = (flags)((uint8_t)flags::REMOTE_WAKE |
                                       (uint8_t)flags::NORMALLY_CONNECTABLE))
        : service(app, f, sec, attributes_.span(), ccc_stores_.span(), BOOT)
    {}

  private:
    std::array<std::span<const uint8_t>, ccc_count(REPORT_PROPS, BOOT)> pending_notify_{};
    c2usb::uninit_store<gatt::attribute, attribute_count(REPORT_PROPS, BOOT)> attributes_;
    c2usb::uninit_store<gatt::ccc_store, ccc_count(REPORT_PROPS, BOOT)> ccc_stores_;
};

} // namespace bluetooth::zephyr::hid

template <>
struct magic_enum::customize::enum_range<bluetooth::zephyr::hid::flags>
{
    static constexpr bool is_flags = true;
};

#endif // C2USB_HAS_ZEPHYR_BT_GATT_HEADERS

#endif // __PORT_ZEPHYR_BLUETOOTH_HID_HPP_
