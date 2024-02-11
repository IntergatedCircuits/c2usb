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
#include "port/zephyr/bluetooth/hid.hpp"
#include "hid/app/keyboard.hpp"
#include "hid/app/mouse.hpp"
#if C2USB_HAS_ZEPHYR_BT_GATT_HEADERS
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(hogp, CONFIG_C2USB_HOGP_LOG_LEVEL);

using namespace magic_enum::bitwise_operators;
using namespace ::hid;
using namespace bluetooth::zephyr;
using namespace bluetooth::zephyr::hid;

#define HOGP_ALREADY_CONNECTED_ERROR BT_ATT_ERR_PROCEDURE_IN_PROGRESS

gatt::permissions service::get_access(security sec)
{
    using namespace bluetooth::zephyr::gatt;
    switch (sec)
    {
    case security::AUTH_ENCRYPT:
        return permissions::READ_AUTHEN | permissions::WRITE_AUTHEN;
    case security::ENCRYPT:
        return permissions::READ_ENCRYPT | permissions::WRITE_ENCRYPT;
    default:
        return permissions::READ | permissions::WRITE;
    }
}
gatt::permissions service::access() const
{
    return access_;
}
gatt::permissions service::read_access() const
{
    using namespace bluetooth::zephyr::gatt;
    return access() & (permissions::READ | permissions::READ_AUTHEN | permissions::READ_ENCRYPT |
                       permissions::READ_LESC);
}
gatt::permissions service::write_access() const
{
    using namespace bluetooth::zephyr::gatt;
    return access() & (permissions::WRITE | permissions::WRITE_AUTHEN | permissions::WRITE_ENCRYPT |
                       permissions::WRITE_LESC);
}

const gatt::char_decl& service::report_map_info()
{
    using namespace bluetooth::zephyr::gatt;
    static const char_decl info{uuid16<BT_UUID_HIDS_REPORT_MAP_VAL>(), properties::READ};
    return info;
}
const gatt::char_decl& service::hid_info()
{
    using namespace bluetooth::zephyr::gatt;
    static const char_decl info{uuid16<BT_UUID_HIDS_INFO_VAL>(), properties::READ};
    return info;
}
const gatt::char_decl& service::protocol_mode_info()
{
    using namespace bluetooth::zephyr::gatt;
    static const char_decl info{uuid16<BT_UUID_HIDS_PROTOCOL_MODE_VAL>(),
                                properties::READ | properties::WRITE_WITHOUT_RESP};
    return info;
}
const gatt::char_decl& service::control_point_info()
{
    using namespace bluetooth::zephyr::gatt;
    static const char_decl info{uuid16<BT_UUID_HIDS_CTRL_POINT_VAL>(),
                                properties::WRITE_WITHOUT_RESP};
    return info;
}
const gatt::char_decl& service::input_report_info()
{
    using namespace bluetooth::zephyr::gatt;
    static const char_decl info{uuid16<BT_UUID_HIDS_REPORT_VAL>(),
                                properties::READ | properties::NOTIFY};
    return info;
}
const gatt::char_decl& service::output_report_info()
{
    using namespace bluetooth::zephyr::gatt;
    static const char_decl info{uuid16<BT_UUID_HIDS_REPORT_VAL>(),
                                properties::READ | properties::WRITE |
                                    properties::WRITE_WITHOUT_RESP};
    return info;
}
const gatt::char_decl& service::feature_report_info()
{
    return output_report_info();
}
const gatt::char_decl& service::boot_keyboard_in_info()
{
    using namespace bluetooth::zephyr::gatt;
    static const char_decl info{uuid16<BT_UUID_HIDS_BOOT_KB_IN_REPORT_VAL>(),
                                properties::READ | properties::NOTIFY};
    return info;
}
const gatt::char_decl& service::boot_keyboard_out_info()
{
    using namespace bluetooth::zephyr::gatt;
    static const char_decl info{uuid16<BT_UUID_HIDS_BOOT_KB_OUT_REPORT_VAL>(),
                                properties::READ | properties::WRITE |
                                    properties::WRITE_WITHOUT_RESP};
    return info;
}
const gatt::char_decl& service::boot_mouse_in_info()
{
    using namespace bluetooth::zephyr::gatt;
    static const char_decl info{uuid16<BT_UUID_HIDS_BOOT_MOUSE_IN_REPORT_VAL>(),
                                properties::READ | properties::NOTIFY};
    return info;
}

ssize_t service::get_report_map(::bt_conn* conn, const ::bt_gatt_attr* attr, void* buf,
                                uint16_t len, uint16_t offset)
{
    auto* this_ = reinterpret_cast<service*>(attr->user_data);
    auto& desc = this_->app_.report_info().descriptor;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, const_cast<uint8_t*>(desc.data()),
                             desc.size());
}

ssize_t service::get_protocol_mode(::bt_conn* conn, const ::bt_gatt_attr* attr, void* buf,
                                   uint16_t len, uint16_t offset)
{
    auto* this_ = reinterpret_cast<service*>(attr->user_data);

    auto protocol = this_->get_protocol();
    LOG_DBG("get protocol: %u", static_cast<uint8_t>(protocol));
    return bt_gatt_attr_read(conn, attr, buf, len, offset, reinterpret_cast<uint8_t*>(&protocol),
                             sizeof(protocol));
}

ssize_t service::set_protocol_mode(::bt_conn* conn, const ::bt_gatt_attr* attr, void const* buf,
                                   uint16_t len, uint16_t offset, uint8_t flags)
{
    auto* this_ = reinterpret_cast<service*>(attr->user_data);

    if (offset > 0)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    if (len > sizeof(::hid::protocol))
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    auto protocol = *(const ::hid::protocol*)buf;
    if (!magic_enum::enum_contains<::hid::protocol>(protocol))
    {
        return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
    }

    LOG_INF("set protocol: %u", static_cast<uint8_t>(protocol));
    if (!this_->start_app(conn, protocol))
    {
        return BT_GATT_ERR(HOGP_ALREADY_CONNECTED_ERROR);
    }
    return len;
}

ssize_t service::control_point_request(::bt_conn* conn, const ::bt_gatt_attr* attr, void const* buf,
                                       uint16_t len, uint16_t offset, uint8_t flags)
{
    auto* this_ = reinterpret_cast<service*>(attr->user_data);

    if (offset > 0)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    if (len > sizeof(uint8_t))
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    auto ev = *(const event*)buf;
    switch (ev)
    {
    case event::SUSPEND:
    case event::EXIT_SUSPEND:
        LOG_INF("control point set: %u", static_cast<uint8_t>(ev));
        if (this_->power_event_delegate_)
        {
            this_->power_event_delegate_(*this_, ev);
        }
        return len;
    default:
        return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
    }
}

void service::request_get_report(::hid::report::selector sel, const std::span<uint8_t>& buffer)
{
    get_report_ = sel;
    get_report_buffer_ = {};
    app_.get_report(sel, buffer);
    get_report_ = {};
}

ssize_t service::get_report(::bt_conn* conn, const gatt::attribute* attr, uint8_t* buf,
                            uint16_t len, uint16_t offset)
{
    LOG_DBG("get report, size:%u, offset:%u", len, offset);

    auto* this_ = reinterpret_cast<service*>(attr->user_data);
    if (!this_->start_app(conn))
    {
        return BT_GATT_ERR(HOGP_ALREADY_CONNECTED_ERROR);
    }

    this_->request_get_report(report_attr_selector(attr), {buf, len});
    auto data = this_->get_report_buffer_;
    if (data.data() == nullptr)
    {
        LOG_WRN("get report failed");
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    auto report_offset = this_->report_data_offset();
    return bt_gatt_attr_read(conn, attr, buf, len, offset, data.data() + report_offset,
                             data.size() - report_offset);
}

ssize_t service::set_report(::bt_conn* conn, const gatt::attribute* attr, const uint8_t* buf,
                            uint16_t len, uint16_t offset, gatt::write_flags flag)
{
    LOG_DBG("set report, size:%u, offset:%u", len, offset);

    auto* this_ = reinterpret_cast<service*>(attr->user_data);
    if (!this_->start_app(conn))
    {
        return BT_GATT_ERR(HOGP_ALREADY_CONNECTED_ERROR);
    }

    auto sel = report_attr_selector(attr);
    auto report_offset = this_->report_data_offset();
    if (sel.type() == report::type::INPUT)
    {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    // split writes are not supported by the HID application design
    if ((offset > 0) or
        ((offset + len) > (this_->app_.report_info().max_report_size(sel.type()) - report_offset)))
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    // copy to application buffer to extend data lifetime
    auto buffer = this_->rx_buffers_[sel.type()];
    if (buffer.size() < (offset + len - report_offset))
    {
        return BT_GATT_ERR(BT_ATT_ERR_WRITE_REQ_REJECTED);
    }
    buffer = buffer.subspan(0, report_offset + len);
    buffer[0] = sel.id(); // if no ids, it will be overwritten
    ::memcpy(buffer.data() + report_offset, buf, len);

    this_->rx_buffers_[sel.type()] = {};
    this_->app_.set_report(sel.type(), buffer);
    return len;
}

ssize_t service::get_boot_report(::bt_conn* conn, const gatt::attribute* attr, uint8_t* buf,
                                 uint16_t len, uint16_t offset)
{
    LOG_DBG("get boot report, size:%u, offset:%u", len, offset);

    auto* this_ = reinterpret_cast<service*>(attr->user_data);
    if (this_->get_protocol() != protocol::BOOT)
    {
        // at least Windows does this, and the driver fails if we reject,
        // so give an answer without involving the app
        static const size_t empty_size = this_->boot_mode_ == boot_protocol_mode::KEYBOARD
                                             ? sizeof(app::keyboard::keys_input_report<0>)
                                             : sizeof(app::mouse::report<0>);
        memset(buf, 0, empty_size);
        return empty_size;
    }

    this_->request_get_report(report::selector(report::type::INPUT), {buf, len});
    auto data = this_->get_report_buffer_;
    if (data.data() == nullptr)
    {
        LOG_WRN("get report failed");
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    return bt_gatt_attr_read(conn, attr, buf, len, offset, data.data(), data.size());
}

ssize_t service::set_boot_report(::bt_conn* conn, const gatt::attribute* attr, const uint8_t* buf,
                                 uint16_t len, uint16_t offset, gatt::write_flags flag)
{
    LOG_DBG("set boot report, size:%u, offset:%u", len, offset);

    auto* this_ = reinterpret_cast<service*>(attr->user_data);
    if (this_->get_protocol() != protocol::BOOT)
    {
        // the protocol characteristic must be written first to boot
        return BT_GATT_ERR(BT_ATT_ERR_WRITE_REQ_REJECTED);
    }

    // split writes are not supported by the HID application design
    if (offset > 0)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    // copy to application buffer to extend data lifetime
    auto buffer = this_->rx_buffers_[report::type::OUTPUT];
    if (buffer.size() < (offset + len))
    {
        return BT_GATT_ERR(BT_ATT_ERR_WRITE_REQ_REJECTED);
    }
    buffer = buffer.subspan(0, len);
    ::memcpy(buffer.data(), buf, len);

    this_->rx_buffers_[report::type::OUTPUT] = {};
    this_->app_.set_report(report::type::OUTPUT, buffer);
    return len;
}

::hid::result service::send_report(const std::span<const uint8_t>& data, report::type type)
{
    // reroute report when in a querying context
    if ((get_report_.type() == type) and
        ((get_report_.id() == 0) or (get_report_.id() == data.front())))
    {
        get_report_buffer_ = data;
        get_report_ = {};
        return result::OK;
    }

    // feature reports can only be sent if a GET_REPORT command is pending
    // output reports cannot be sent
    if (type != report::type::INPUT)
    {
        return result::INVALID;
    }

    // HOGP would allow parallel report notifications (with different report IDs),
    // but rather keep the application compatible with other transports
    if (input_buffer_.size() > 0)
    {
        return result::BUSY;
    }

    const gatt::attribute* attr;
    size_t offset = 0;
    if (app_.get_protocol() == protocol::REPORT)
    {
        attr = input_report_attr(app_.report_info().uses_report_ids() ? data.front() : 0);
        offset = report_data_offset();
    }
    else
    {
        attr = input_boot_attr();
    }
    if (attr == nullptr)
    {
        return result::INVALID;
    }

    auto ret = attr->notify(
        data.subspan(offset),
        [](::bt_conn*, void* user_data)
        {
            auto* this_ = reinterpret_cast<service*>(user_data);
            auto buf = this_->input_buffer_;
            this_->input_buffer_ = {};
            LOG_DBG("input report sent (size %u)", buf.size());
            this_->app_.in_report_sent(buf);
        },
        this, active_conn_.load());
    switch (ret)
    {
    case 0:
        input_buffer_ = data;
        return result::OK;
    case -ENOENT:
    case -EINVAL:
        return result::INVALID;
    default:
        return result::NO_CONNECTION;
    }
}

::hid::result service::receive_report(const std::span<uint8_t>& data, ::hid::report::type type)
{
    // the transfer is initiated by the other end
    rx_buffers_[type] = data;
    return result::OK;
}

ssize_t service::ccc_cfg_write(::bt_conn* conn, const gatt::attribute* attr, gatt::ccc_flags flags)
{
    // a single connection is allowed to interact with HOGP at a time
    // to avoid conflicting protocol modes

    auto protocol = protocol::BOOT;
    attr -= 1; // distance between characteristic value and ccc descriptor (in boot mode)
    if (attr->uuid == &uuid16<BT_UUID_HIDS_REPORT_REF_VAL>())
    {
        attr -= 1; // skip the report reference descriptor if it's present
        protocol = protocol::REPORT;
        LOG_DBG("report CCC set: %u", static_cast<uint16_t>(flags));
    }
    else
    {
        LOG_DBG("boot CCC set: %u", static_cast<uint16_t>(flags));
    }
    auto* this_ = reinterpret_cast<service*>(attr->user_data);

    // clearing the flag has no conditions nor consequences
    if (flags == gatt::ccc_flags::NONE)
    {
        return sizeof(flags);
    }

    // prevent changing protocol mode
    if ((protocol == protocol::BOOT) && (this_->get_protocol() != protocol))
    {
        return BT_GATT_ERR(BT_ATT_ERR_WRITE_REQ_REJECTED);
    }

    return this_->start_app(conn, protocol) ? sizeof(flags)
                                            : BT_GATT_ERR(HOGP_ALREADY_CONNECTED_ERROR);
}

gatt::attribute::builder service::add_input_report(gatt::attribute::builder attr_tail,
                                                   gatt::ccc_store* ccc, ::hid::report::id::type id)
{
    using namespace ::hid::report;

    *ccc = gatt::ccc_store(nullptr, &service::ccc_cfg_write, nullptr);

    return attr_tail
        .characteristic(input_report_info(), read_access(), &service::get_report, nullptr, this)
        .descriptor(uuid16<BT_UUID_HIDS_REPORT_REF_VAL>(), read_access(), selector(type::INPUT, id))
        .ccc_descriptor(*ccc, access());
}

gatt::attribute::builder service::add_output_report(gatt::attribute::builder attr_tail,
                                                    ::hid::report::id::type id)
{
    using namespace ::hid::report;

    return attr_tail
        .characteristic(output_report_info(), write_access(), nullptr, &service::set_report, this)
        .descriptor(uuid16<BT_UUID_HIDS_REPORT_REF_VAL>(), read_access(),
                    selector(type::OUTPUT, id));
}

gatt::attribute::builder service::add_feature_report(gatt::attribute::builder attr_tail,
                                                     ::hid::report::id::type id)
{
    using namespace ::hid::report;

    return attr_tail
        .characteristic(feature_report_info(), access(), &service::get_report, &service::set_report,
                        this)
        .descriptor(uuid16<BT_UUID_HIDS_REPORT_REF_VAL>(), read_access(),
                    selector(type::FEATURE, id));
}

void service::connected(::bt_conn* conn) {}

void service::disconnected(::bt_conn* conn)
{
    stop_app(conn);
}

::hid::protocol service::get_protocol()
{
    auto protocol = protocol::REPORT;
    if (app_.has_transport(this))
    {
        protocol = app_.get_protocol();
    }
    return protocol;
}

bool service::start_app(::bt_conn* conn, ::hid::protocol protocol)
{
    ::bt_conn* expected = nullptr;
    if (!active_conn_.compare_exchange_strong(expected, conn))
    {
        if (expected != conn)
        {
            // other connection has claimed the app already
            return false;
        }
        if (protocol == app_.get_protocol())
        {
            // correct protocol is active
            return true;
        }
        // restart with new protocol
        app_.teardown(this);
    }

    auto result = app_.setup(this, protocol);
    LOG_INF("starting HID app %u: %u", static_cast<uint8_t>(protocol), result);
    if (!result)
    {
        active_conn_.compare_exchange_strong(conn, nullptr);
    }
    return result;
}

void service::stop_app(::bt_conn* conn)
{
    if (!active_conn_.compare_exchange_strong(conn, nullptr))
    {
        return;
    }

    auto result = app_.teardown(this);
    LOG_INF("stopping HID app %u", result);
}

std::span<gatt::attribute> service::fill_attributes(const std::span<gatt::attribute>& attrs,
                                                    const std::span<gatt::ccc_store>& cccs, flags f)
{
    auto* ccc_ptr = cccs.data();

    // base service attributes
    auto attr_tail =
        gatt::attribute::builder(attrs.data())
            .primary_service(uuid16<BT_UUID_HIDS_VAL>())
            .characteristic(report_map_info(), read_access(),
                            //&gatt::attribute::read_range<report_protocol::descriptor_view_type>,
                            // nullptr, &app_.report_info().descriptor)
                            &service::get_report_map, nullptr, this)
            .characteristic(hid_info(), read_access(), hid::info(f))
            .characteristic(protocol_mode_info(), access(), &service::get_protocol_mode,
                            &service::set_protocol_mode,
                            this) // keep "this" here as long as it's used by for_each()
            .characteristic(control_point_info(), write_access(), nullptr,
                            &service::control_point_request, this);

    assert(attr_tail.data() == (attrs.data() + base_attribute_count()));

    // report protocol attributes
    if (app_.report_info().max_input_size > 0)
    {
        if (!app_.report_info().uses_report_ids())
        {
            attr_tail = add_input_report(attr_tail, ccc_ptr);
            ccc_ptr++;
        }
        else
        {
            for (auto id = ::hid::report::id::min(); id <= app_.report_info().max_input_id; id++)
            {
                attr_tail = add_input_report(attr_tail, ccc_ptr, id);
                ccc_ptr++;
            }
        }
    }
    if (app_.report_info().max_output_size > 0)
    {
        if (!app_.report_info().uses_report_ids())
        {
            attr_tail = add_output_report(attr_tail);
        }
        else
        {
            for (auto id = ::hid::report::id::min(); id <= app_.report_info().max_output_id; id++)
            {
                attr_tail = add_output_report(attr_tail, id);
            }
        }
    }
    if (app_.report_info().max_feature_size > 0)
    {
        if (!app_.report_info().uses_report_ids())
        {
            attr_tail = add_feature_report(attr_tail);
        }
        else
        {
            for (auto id = ::hid::report::id::min(); id <= app_.report_info().max_feature_id; id++)
            {
                attr_tail = add_feature_report(attr_tail, id);
            }
        }
    }

    // boot protocol attributes
    if (boot_mode_ != boot_protocol_mode::NONE)
    {
        auto& ccc = *ccc_ptr;
        ccc = gatt::ccc_store(nullptr, &service::ccc_cfg_write, nullptr);
        ccc_ptr++;

        if (boot_mode_ == boot_protocol_mode::KEYBOARD)
        {
            attr_tail = attr_tail
                            .characteristic(boot_keyboard_out_info(), write_access(), nullptr,
                                            &service::set_boot_report, this)
                            .characteristic(boot_keyboard_in_info(), read_access(),
                                            &service::get_boot_report, nullptr, this)
                            .ccc_descriptor(ccc, access());
        }
        else // boot_mode_ == boot_protocol_mode::MOUSE
        {
            attr_tail = attr_tail
                            .characteristic(boot_mouse_in_info(), read_access(),
                                            &service::get_boot_report, nullptr, this)
                            .ccc_descriptor(ccc, access());
        }
    }
    assert(attr_tail.data() == (attrs.data() + attrs.size()));
    assert(ccc_ptr == (cccs.data() + cccs.size()));

    return attrs;
}

const gatt::attribute* service::input_report_attr(::hid::report::id::type id) const
{
    if (app_.report_info().max_input_size == 0)
    {
        return nullptr;
    }

    const gatt::attribute* attr = &attributes()[base_attribute_count()];
    if (id != 0)
    {
        if ((id > app_.report_info().max_input_id) or (id < report::id::min()))
        {
            return nullptr;
        }
        attr += report_attribute_count(report::type::INPUT) * (id - report::id::min());
    }
    assert(attr[report_reference_offset()].user_value<report::selector>() ==
           report::selector(report::type::INPUT, id));
    return attr;
}

const gatt::attribute* service::input_boot_attr() const
{
    auto attrs = attributes();
    auto* attr = &attrs[attrs.size() - boot_attribute_count(report::type::INPUT)];
    return attr;
}

service* service::base_from_input_report_attr(const gatt::attribute* attr)
{
    auto sel = attr[report_reference_offset()].user_value<report::selector>();
    assert(sel.type() == report::type::INPUT);
    uint8_t offset = 0;
    if (sel.id() > report::id::min())
    {
        offset = sel.id() - report::id::min();
        attr -= report_attribute_count(report::type::INPUT) * offset;
    }
    attr -= base_attribute_count();
    return base_from_service_attr(attr);
}

service* service::base_from_service_attr(const gatt::attribute* attr)
{
    struct base_finder : public service
    {
        using service::service;
        gatt::attribute field;
    };
    return CONTAINER_OF(attr, base_finder, field);
}

BT_CONN_CB_DEFINE(hid_service_conn_callbacks) = {
    .disconnected = [](::bt_conn* conn, uint8_t reason)
    { service::for_each<::bt_conn*, &service::disconnected>(conn); },
};

#endif // C2USB_HAS_ZEPHYR_BT_HEADERS
