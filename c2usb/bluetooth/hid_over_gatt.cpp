// SPDX-License-Identifier: MPL-2.0
#include "bluetooth/hid_over_gatt.hpp"
#include <ranges>
#include <hid/app/keyboard.hpp>
#include <hid/app/mouse.hpp>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(hogp, CONFIG_C2USB_HOGP_LOG_LEVEL);

using namespace magic_enum::bitwise_operators;
using namespace hid;

namespace bluetooth::hid_over_gatt
{

service::conn_session& service::make_session(::bt_conn* conn, hid::protocol prot)
{
    auto it = std::ranges::find_if(sessions_, [&](const auto& s) { return s.conn == conn; });
    if (it == sessions_.end())
    {
        it = std::ranges::find_if(sessions_, [](const auto& s) { return s.conn == nullptr; });
        assert((it != sessions_.end()) and "no free conn_session slots");
    }
    const session_params params{this, channel::BT_GATT,
                                (prot == hid::protocol::BOOT) ? boot_mode() : hid::boot::mode::NONE,
                                conn};
    it->conn = conn;
    transport::start(app_, it->session, params);
    return *it;
}

ssize_t service::get_report_map(::bt_conn* conn, const ::bt_gatt_attr* attr, void* buf,
                                uint16_t len, uint16_t offset)
{
    auto* self = static_cast<service*>(attr->user_data);
    auto desc = self->app_.report_info().descriptor;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, const_cast<uint8_t*>(desc.data()),
                             desc.size());
}

ssize_t service::get_protocol_mode(::bt_conn* conn, const ::bt_gatt_attr* attr, void* buf,
                                   uint16_t len, uint16_t offset)
{
    auto* self = static_cast<service*>(attr->user_data);
    auto it = std::ranges::find_if(self->sessions_, [&](const auto& s) { return s.conn == conn; });
    auto protocol = (it != self->sessions_.end()) ? it->session->protocol() : hid::protocol::REPORT;
    LOG_DBG("get protocol: %u", static_cast<uint8_t>(protocol));
    return bt_gatt_attr_read(conn, attr, buf, len, offset, reinterpret_cast<uint8_t*>(&protocol),
                             sizeof(protocol));
}

ssize_t service::set_protocol_mode(::bt_conn* conn, const ::bt_gatt_attr* attr, void const* buf,
                                   uint16_t len, uint16_t offset, uint8_t flags)
{
    auto* self = static_cast<service*>(attr->user_data);
    if (offset > 0)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    if (len > sizeof(hid::protocol))
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    auto protocol = *(const hid::protocol*)buf;
    if (!magic_enum::enum_contains<hid::protocol>(protocol))
    {
        return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
    }
    self->make_session(conn, protocol);
    LOG_INF("protocol set to: %u", static_cast<uint8_t>(protocol));
    return len;
}

ssize_t service::control_point_request(::bt_conn* conn, const ::bt_gatt_attr* attr, void const* buf,
                                       uint16_t len, uint16_t offset, uint8_t flags)
{
    if (offset > 0)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    if (len > sizeof(uint8_t))
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    auto cmd = *(const uint8_t*)buf;
    LOG_INF("control point set: %u", cmd);
    switch (cmd)
    {
    case uint8_t(event::SUSPEND):
    case uint8_t(event::EXIT_SUSPEND):
#if CONFIG_C2USB_HOGP_POWER_EVENT
        if (auto* self = static_cast<service*>(attr->user_data); self->power_event_delegate_)
        {
            self->power_event_delegate_(*self, static_cast<event>(cmd));
        }
#endif
        return len;
    default:
        return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
    }
}

hid::report::properties service::report_attr_props(const gatt::attribute* attr)
{
    if (attr[report_reference_offset() - 1].uuid == &uuid16<BT_UUID_HIDS_REPORT_REF_VAL>())
    {
        return attr[report_reference_offset() - 1].user_value<report::properties>();
    }
#if CONFIG_C2USB_HID_BOOT_PROTOCOL
    if (attr->uuid == &uuid16<BT_UUID_HIDS_BOOT_KB_IN_REPORT_VAL>())
    {
        return hid::report::properties{hid::report::selector(report::type::INPUT),
                                       sizeof(hid::app::keyboard::boot_input_report)};
    }
    if (attr->uuid == &uuid16<BT_UUID_HIDS_BOOT_KB_OUT_REPORT_VAL>())
    {
        return hid::report::properties{hid::report::selector(report::type::OUTPUT),
                                       sizeof(hid::app::keyboard::boot_output_report)};
    }
    if (attr->uuid == &uuid16<BT_UUID_HIDS_BOOT_MOUSE_IN_REPORT_VAL>())
    {
        return hid::report::properties{hid::report::selector(report::type::INPUT),
                                       sizeof(hid::app::mouse::boot_report)};
    }
#endif
    assert(false and "unknown report attribute");
    return hid::report::properties{};
}

ssize_t service::get_report(::bt_conn* conn, const gatt::attribute* attr, uint8_t* buf,
                            uint16_t len, uint16_t offset)
{
    auto props = report_attr_props(attr);
    LOG_DBG("get report %x, size:%u, offset:%u", std::bit_cast<uint16_t>(props.selector), len,
            offset);
    auto* self = static_cast<service*>(attr->user_data);
    return self->get_report_data(*self->make_session(conn, hid::protocol::REPORT).session, props,
                                 buf, len, offset);
}

ssize_t service::get_report_data(hid::session& sess, hid::report::properties props, uint8_t* buf,
                                 uint16_t len, uint16_t offset)
{
    auto data = sess.get_report(props.selector, std::span<uint8_t>(buf, len));
    if (data.empty())
    {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    size_t report_offset = 0;
    if (props.selector.id() != 0)
    {
        report_offset = sizeof(props.selector.id());
    }
    if ((report_offset + offset) > data.size())
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    auto begin = data.data() + report_offset + offset;
    len = std::min<uint16_t>(len, data.size() - report_offset - offset);
    std::move(begin, begin + len, buf);
    return static_cast<ssize_t>(len);
}

ssize_t service::set_report(::bt_conn* conn, const gatt::attribute* attr, const uint8_t* buf,
                            uint16_t len, uint16_t offset, gatt::write_flags flag)
{
    auto props = report_attr_props(attr);
    LOG_DBG("set report %x, size:%u, offset:%u", std::bit_cast<uint16_t>(props.selector), len,
            offset);

    auto* self = static_cast<service*>(attr->user_data);
    return self->set_report_data(self->make_session(conn, hid::protocol::REPORT), props, buf, len,
                                 offset);
}

ssize_t service::set_report_data(conn_session& conns, hid::report::properties props,
                                 const uint8_t* buf, uint16_t len, uint16_t offset)
{
    auto buffer = conns.rx_buffers_[props.selector.type()];

    // the data must not exceed report size, nor the provided buffer's size
    auto size = std::min<uint16_t>(buffer.size(), props.size);

    size_t report_offset = 0;
    if (props.selector.id() != 0)
    {
        buffer[0] = props.selector.id();
        report_offset = sizeof(props.selector.id());
    }
    if (size < (report_offset + offset))
    {
        return offset ? BT_GATT_ERR(BT_ATT_ERR_WRITE_REQ_REJECTED)
                      : BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    len = std::min<uint16_t>(len, size - report_offset - offset);
    std::copy_n(buf, len, buffer.data() + report_offset + offset);

    // when size is reached, consider report data complete, pass to application
    if ((report_offset + offset + len) >= size)
    {
        conns.rx_buffers_[props.selector.type()] = {};
        conns.session->set_report(props.selector.type(), buffer);
    }
    return static_cast<ssize_t>(len);
}

#if CONFIG_C2USB_HID_BOOT_PROTOCOL
ssize_t service::get_boot_report(::bt_conn* conn, const gatt::attribute* attr, uint8_t* buf,
                                 uint16_t len, uint16_t offset)
{
    auto props = report_attr_props(attr);
    LOG_DBG("get boot report %x, size:%u, offset:%u", std::bit_cast<uint16_t>(props.selector), len,
            offset);
    auto* self = static_cast<service*>(attr->user_data);
    auto it = std::ranges::find_if(self->sessions_, [&](const auto& s) { return s.conn == conn; });

    // give an empty report if boot protocol isn't active
    if ((it == self->sessions_.end()) or (it->session->protocol() != hid::protocol::BOOT))
    {
        auto size = std::min<uint16_t>(len, props.size - offset);
        memset(buf, 0, size);
        return size;
    }
    return self->get_report_data(*it->session, props, buf,
                                 // use the boot report size as an indication of whether the
                                 // boot keyboard or mouse report is requested
                                 std::min<uint16_t>(len, props.size), offset);
}

ssize_t service::set_boot_report(::bt_conn* conn, const gatt::attribute* attr, const uint8_t* buf,
                                 uint16_t len, uint16_t offset, gatt::write_flags flag)
{
    auto props = report_attr_props(attr);
    LOG_DBG("set boot report %x, size:%u, offset:%u", std::bit_cast<uint16_t>(props.selector), len,
            offset);

    auto* self = static_cast<service*>(attr->user_data);
    auto it = std::ranges::find_if(self->sessions_, [&](const auto& s) { return s.conn == conn; });

    // reject write if boot protocol isn't active
    if ((it == self->sessions_.end()) or (it->session->protocol() != hid::protocol::BOOT))
    {
        return BT_GATT_ERR(BT_ATT_ERR_WRITE_REQ_REJECTED);
    }
    return self->set_report_data(*it, props, buf, len, offset);
}

const gatt::attribute* service::input_boot_attr(hid::boot::mode bm) const
{
    if ((boot_mode() & bm) == hid::boot::mode::NONE)
    {
        return nullptr;
    }
    auto attrs = attributes();
    auto* attr = &attrs[attrs.size() - boot_attribute_count(report::type::INPUT)];
    return attr + 1;
}
#endif

const gatt::attribute* service::input_report_attr(hid::report::id::type id) const
{
    report::selector sel{report::type::INPUT, id};
    for (const gatt::attribute* attr = &attributes()[base_attribute_count()];
         attr[report_reference_offset() + 1].uuid == &uuid16<BT_UUID_GATT_CCC_VAL>();
         attr += report_attribute_count(report::type::INPUT))
    {
        if (attr[report_reference_offset()].user_value<report::selector>() == sel)
        {
            return attr + 1;
        }
    }
    return nullptr;
}

c2usb::result service::receive_report(session& sess, const std::span<uint8_t>& data,
                                      report::type type)
{
    auto it = std::ranges::find_if(sessions_, [&](const auto& s) { return s.session == &sess; });
    if (it == sessions_.end())
    {
        // race condition at make_session():
        // session constructor can call before it is added to the sessions_ array
        it = std::ranges::find_if(sessions_, [&](const auto& s)
                                  { return s.session == nullptr and s.conn != nullptr; });
    }
    if (it == sessions_.end())
    {
        return c2usb::result::broken_pipe;
    }
    it->rx_buffers_[type] = data;
    return c2usb::result::ok;
}

std::span<const uint8_t>& service::get_pending_notify(const gatt::attribute* attr)
{
    attr += report_reference_offset() - 1;
    if (!IS_ENABLED(CONFIG_C2USB_HID_BOOT_PROTOCOL) or
        (attr->uuid == &uuid16<BT_UUID_HIDS_REPORT_REF_VAL>()))
    {
        // if we find the report reference descriptor, next is the CCC descriptor
        // otherwise it's a boot report, where the CCC is this one
        attr++;
    }
    // now attr points to the CCC descriptor
    assert(attr->uuid == &uuid16<BT_UUID_GATT_CCC_VAL>());
    return static_cast<ccc_data*>(const_cast<void*>(attr->user_data))->pending_notify;
}

c2usb::result service::send_report(session& sess, const std::span<const uint8_t>& data)
{
    const gatt::attribute* attr = nullptr;
    size_t offset = 0;
    if (!IS_ENABLED(CONFIG_C2USB_HID_BOOT_PROTOCOL) or sess.protocol() == protocol::REPORT)
    {
        offset = report_data_offset();
        attr = input_report_attr(offset ? data.front() : 0);
    }
#if CONFIG_C2USB_HID_BOOT_PROTOCOL
    else if (data.size() == sizeof(hid::app::keyboard::boot_input_report))
    {
        attr = input_boot_attr(hid::boot::mode::KEYBOARD);
    }
    else if (data.size() == sizeof(hid::app::mouse::boot_report))
    {
        attr = input_boot_attr(hid::boot::mode::MOUSE);
    }
#endif
    if (attr == nullptr)
    {
        return c2usb::result::invalid_argument;
    }

    auto it = std::ranges::find_if(sessions_, [&](const auto& s) { return s.session == &sess; });
    if (it == sessions_.end())
    {
        return c2usb::result::broken_pipe;
    }

    // alternatively we could allow queueing more than one notification per characteristic,
    // at the cost of losing track of what message was sent, breaking report_sent() callback
    auto& pending_notify = get_pending_notify(attr);
    if (pending_notify.size() > 0)
    {
        return c2usb::result::device_or_resource_busy;
    }

    pending_notify = data;
    auto result = c2usb::result(attr->notify(
        data.subspan(offset),
        [](::bt_conn* conn, void* user_data)
        {
            auto* attr = static_cast<const gatt::attribute*>(user_data);
            auto* self = static_cast<service*>(attr->user_data);
            self->notify_callback(conn, attr);
        },
        attr, it->conn));
    if (result != c2usb::result::ok)
    {
        pending_notify = {};
    }
    return result;
}

void service::notify_callback(::bt_conn* conn, const gatt::attribute* attr)
{
    // clear pending notify before the callback
    auto& pending_notify = get_pending_notify(attr);
    auto buf = pending_notify;
    pending_notify = {};
    LOG_DBG("input report %x sent (size %u)",
            std::bit_cast<uint16_t>(report_attr_props(attr).selector), buf.size());

    if (auto it = std::ranges::find_if(sessions_, [&](const auto& s) { return s.conn == conn; });
        it != sessions_.end())
    {
        it->session->report_sent(buf);
    }
}

ssize_t service::ccc_cfg_write(::bt_conn* conn, const gatt::attribute* attr, gatt::ccc_flags flags)
{
    attr -= 1; // distance between characteristic value and ccc descriptor (in boot mode)
    hid::protocol prot = hid::protocol::BOOT;
    if (!IS_ENABLED(CONFIG_C2USB_HID_BOOT_PROTOCOL) or
        (attr->uuid == &uuid16<BT_UUID_HIDS_REPORT_REF_VAL>()))
    {
        attr -= 1; // skip the report reference descriptor if it's present
        prot = hid::protocol::REPORT;
    }
    auto* self = static_cast<service*>(attr->user_data);

    LOG_DBG("CCC %x set: %u", std::bit_cast<uint16_t>(report_attr_props(attr).selector),
            static_cast<uint16_t>(flags));
    auto& session = *self->make_session(conn, prot).session;
    return (session.protocol() == prot) ? sizeof(flags) : BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
}

void service::disconnected(::bt_conn* conn)
{
    auto it = std::ranges::find_if(sessions_, [&](const auto& s) { return s.conn == conn; });
    if (it != sessions_.end())
    {
        transport::stop(app_, it->session);
        *it = {}; // reset conn_session, setting session and conn to nullptr
    }
}

void service::disconnect_callback(::bt_conn* conn, uint8_t reason)
{
    for_each<::bt_conn*, &service::disconnected>(conn);
}

} // namespace bluetooth::hid_over_gatt

#if CONFIG_C2USB_HOGP_BT_DISCONN_CB
BT_CONN_CB_DEFINE(hid_over_gatt_conn_callbacks) = {
    .disconnected = &bluetooth::hid_over_gatt::service::disconnect_callback,
};
#endif
