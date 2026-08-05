// SPDX-License-Identifier: MPL-2.0
#include "bluetooth/hid_over_gatt.hpp"
#include <ranges>
#include <hid/app/keyboard.hpp>
#include <hid/app/mouse.hpp>
#include <zephyr/logging/log.h>
#if CONFIG_C2USB_HOGP_SCI
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_types.h>
#endif

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
    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                             c2usb::std_layout_cast<uint8_t*>(&protocol), sizeof(protocol));
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
    if (len != sizeof(uint8_t))
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
#if defined(CONFIG_C2USB_HOGP_SCI)
    case uint8_t(sci_mode::DEFAULT):
    case uint8_t(sci_mode::FAST):
    case uint8_t(sci_mode::FULL_RANGE):
    case uint8_t(sci_mode::LOW_POWER):
        return static_cast<service*>(attr->user_data)
                       ->set_sci_mode(conn, static_cast<sci_mode>(cmd))
                   ? len
                   : BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
#endif
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

#if CONFIG_C2USB_HOGP_SCI
ssize_t service::get_sci_information(::bt_conn* conn, const ::bt_gatt_attr* attr, void* buf,
                                     uint16_t len, uint16_t offset)
{
    if (sci_attributes_.min_supported_conn_interval == 0)
    {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                             c2usb::std_layout_cast<const uint8_t*>(&sci_attributes_),
                             offsetof(decltype(sci_attributes_), groups) +
                                 sci_attributes_.num_groups * sizeof(sci_attributes_.groups[0]));
}

ssize_t service::get_sci_mode(::bt_conn* conn, const ::bt_gatt_attr* attr, void* buf, uint16_t len,
                              uint16_t offset)
{
    auto* self = static_cast<service*>(attr->user_data);
    auto it = std::ranges::find_if(self->sessions_, [&](const auto& s) { return s.conn == conn; });
    if (it == self->sessions_.end())
    {
        uint8_t mode = uint8_t(sci_mode::NONE);
        return bt_gatt_attr_read(conn, attr, buf, len, offset, &mode, sizeof(mode));
    }
    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                             c2usb::std_layout_cast<const uint8_t*>(&it->active_sci_mode_),
                             sizeof(it->active_sci_mode_));
}

bool service::set_sci_mode(::bt_conn* conn, sci_mode mode)
{
    if (sci_attributes_.min_supported_conn_interval == 0)
    {
        return false;
    }
    auto it = std::ranges::find_if(sessions_, [&](const auto& s) { return s.conn == conn; });
    if (it == sessions_.end())
    {
        // no session yet, use a slot for this connection, without a session
        auto it = std::ranges::find_if(sessions_, [&](const auto& s) { return s.conn == nullptr; });
        assert((it != sessions_.end()) and "no free conn_session slots");
        it->conn = conn;
    }

    auto params = std::find_if(sci_mode_params_.begin(), sci_mode_params_.end(),
                               [mode](const auto& p) { return p.mode == mode; });
    if (params == sci_mode_params_.end())
    {
        assert((mode == sci_mode::LOW_POWER) and "Missing mandatory SCI mode parameters");
        return false;
    }

    auto& sci_params = *params;
    if (auto ret = bt_conn_le_conn_rate_request(conn, &sci_params); ret != 0)
    {
        LOG_ERR("Failed to request connection rate change: %d", ret);
        return false;
    }

    it->pending_sci_mode_ = mode;
    return true;
}

void service::connect_callback(bt_conn* conn, uint8_t err)
{
    if (sci_attributes_.min_supported_conn_interval == 0)
    {
        // first time initialization, fetch and store the SCI information for all subsequent use
        const size_t offset =
            offsetof(::bt_hci_op_le_read_min_supported_conn_interval, min_supported_conn_interval);
        struct net_buf* rsp;
        if (auto ret =
                bt_hci_cmd_send_sync(BT_HCI_OP_LE_READ_MIN_SUPPORTED_CONN_INTERVAL, nullptr, &rsp);
            (ret != 0) or (rsp->len < offset + sizeof(sci_attributes<0>)))
        {
            LOG_ERR("Failed to read min supported connection interval: %d", ret);
            return;
        }

        size_t size = std::min<size_t>(sizeof(sci_attributes_), rsp->len - offset);
        std::copy_n(rsp->data + offset, size,
                    c2usb::std_layout_cast<uint8_t*>(&sci_attributes_.min_supported_conn_interval));

        size -= offsetof(decltype(sci_attributes_), groups);
        if (sci_attributes_.num_groups > sci_attributes_.groups.size())
        {
            LOG_WRN("Supported connection interval groups exceeds storage: %d (max %d)",
                    sci_attributes_.num_groups, sci_attributes_.groups.size());
            sci_attributes_.num_groups = sci_attributes_.groups.size();
        }

        net_buf_unref(rsp);
    }
}

void service::connection_rate_callback(::bt_conn* conn, uint8_t status,
                                       const ::bt_conn_le_conn_rate_changed* params)
{
    if (status != BT_HCI_ERR_SUCCESS)
    {
        return;
    }
    auto change_params = connection_rate_params{conn, params};
    for_each<const connection_rate_params, &service::connection_rate_changed>(&change_params);
}

void service::connection_rate_changed(const connection_rate_params* p)
{
    auto it = std::ranges::find_if(sessions_, [&](const auto& s) { return s.conn == p->conn; });
    if (it == sessions_.end())
    {
        return;
    }
    auto new_mode = it->pending_sci_mode_;
    it->pending_sci_mode_ = sci_mode::NONE;
    if (new_mode != sci_mode::NONE)
    {
        auto mode_params = std::find_if(sci_mode_params_.begin(), sci_mode_params_.end(),
                                        [new_mode](const auto& p) { return p.mode == new_mode; });
        if ((mode_params == sci_mode_params_.end()) or !mode_params->match(*p->params))
        {
            LOG_WRN("Connection %p rate change does not match requested SCI mode %u", p->conn,
                    uint8_t(new_mode));
            new_mode = sci_mode::NONE;
        }
    }
    if (new_mode == sci_mode::NONE)
    {
        // no pending SCI mode change request, find a matching mode
        auto matching_params =
            std::find_if(sci_mode_params_.begin(), sci_mode_params_.end(),
                         [in = p->params](const auto& p) { return p.match(*in); });
        new_mode =
            (matching_params != sci_mode_params_.end()) ? matching_params->mode : sci_mode::NONE;
    }

    if (new_mode != it->active_sci_mode_)
    {
        it->active_sci_mode_ = new_mode;
        LOG_INF("Connection %p SCI mode changed to %u", p->conn, uint8_t(new_mode));
        sci_mode_attr()->notify(
            std::span<const uint8_t>(c2usb::std_layout_cast<const uint8_t*>(&new_mode),
                                     sizeof(new_mode)),
            p->conn);
    }
}
#endif

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
    for_each<::bt_conn, &service::disconnected>(conn);
}

} // namespace bluetooth::hid_over_gatt

BT_CONN_CB_DEFINE(hid_over_gatt_conn_callbacks) = {
#if CONFIG_C2USB_HOGP_SCI
    .connected = &bluetooth::hid_over_gatt::service::connect_callback,
#endif
    .disconnected = &bluetooth::hid_over_gatt::service::disconnect_callback,
#if CONFIG_C2USB_HOGP_SCI
    .conn_rate_changed = &bluetooth::hid_over_gatt::service::connection_rate_callback,
#endif
};
