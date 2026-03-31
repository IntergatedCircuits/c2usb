// SPDX-License-Identifier: MPL-2.0
#include "usb/df/class/hid.hpp"
#include "usb/df/message.hpp"
#include "usb/standard/descriptors.hpp"
#include <magic_enum.hpp>

using namespace ::hid;
using namespace usb;

namespace usb::df::hid
{
void app_base_function::start(const config::interface& iface, ::hid::boot::mode prot)
{
    disable(iface);

    // open endpoints
    open_eps(iface.endpoints(), ephs_);
    assert(ep_in_handle().valid());

    // start application
    transport::start(app_, session_, {this, channel::USB, prot});
}

void app_base_function::disable([[maybe_unused]] const config::interface& iface)
{
    transport::stop(app_, session_);
    close_eps(ephs_);
}

c2usb::result app_base_function::send_report([[maybe_unused]] ::hid::session& sess,
                                             const std::span<const uint8_t>& data)
{
    return send_ep(ep_in_handle(), data);
}

c2usb::result app_base_function::receive_report([[maybe_unused]] ::hid::session& sess,
                                                const std::span<uint8_t>& data,
                                                ::hid::report::type type)
{
    if ((type == report::type::OUTPUT) and ep_out_handle().valid())
    {
        // if has out ep, try to start receiving
        return receive_ep(ep_out_handle(), data);
    }
    else
    {
        // otherwise save the buffer for control transfer
        rx_buffers_[type] = data;
        return result::ok;
    }
}

void app_base_function::ep_callback(const transfer& t)
{
    if (t.endpoint() == ep_in_handle())
    {
        if (session_)
        {
            session_->report_sent(std::span<const uint8_t>(t.data(), t.size() * t.success()));
        }
    }
    else if (t.endpoint() == ep_out_handle())
    {
        if (session_)
        {
            session_->set_report(report::type::OUTPUT,
                                 std::span<const uint8_t>(t.data(), t.size() * t.success()));
        }
    }
}

void function::get_hid_descriptor(df::buffer& buffer)
{
    auto* hid_desc = buffer.allocate<hid::descriptor::hid<1>>();

    auto* report_subdesc = &hid_desc->ClassDescriptors[0];
    report_subdesc->bDescriptorType = hid::descriptor::type::REPORT;
    report_subdesc->wItemLength = app_.report_info().descriptor.size();
}

void function::describe_config(const config::interface& iface, uint8_t if_index, df::buffer& buffer)
{
    auto* iface_desc = buffer.allocate<standard::descriptor::interface>();

    get_hid_descriptor(buffer);

    iface_desc->bInterfaceNumber = if_index;
    iface_desc->bInterfaceClass = CLASS_CODE;
    iface_desc->bInterfaceSubClass = protocol_mode() != boot_protocol_mode::NONE;
    iface_desc->bInterfaceProtocol = static_cast<uint8_t>(protocol_mode());
    iface_desc->iInterface = name_istring();
    iface_desc->bNumEndpoints = describe_endpoints(iface, buffer);
    assert((iface.endpoints()[0].address().direction() == direction::IN) and
           ((1 == iface_desc->bNumEndpoints) or
            ((2 == iface_desc->bNumEndpoints) and
             (iface.endpoints()[1].address().direction() == direction::OUT))));
}

void function::get_descriptor(message& msg)
{
    switch (static_cast<hid::descriptor::type>(msg.request().wValue.high_byte()))
    {
    case hid::descriptor::type::HID:
        get_hid_descriptor(msg.buffer());
        return msg.send_buffer();

    case hid::descriptor::type::REPORT:
        return msg.send_data(app_.report_info().descriptor.to_span());

    default:
        return msg.reject();
    }
}

void function::control_setup_request(message& msg, const config::interface& iface)
{
    using namespace standard::interface;
    using namespace hid::control;
    static constexpr uint8_t idle_rate_ms_multiplier = 4;

    if ((session_ == nullptr) and (msg.request() != SET_PROTOCOL) and
        (msg.request() != GET_PROTOCOL))
    {
        /* All tested hosts send at least one of these requests at the beginning:
         * - GET_DESCRIPTOR(REPORT)
         * - SET_IDLE
         * - SET_PROTOCOL
         * So start the application on the first of these (instead of
         * at the function start, which doesn't tolerate longer lasting application init). */
        app_base_function::start(iface, ::hid::boot::mode::NONE);
    }

    auto value_lb = msg.request().wValue.low_byte();
    switch (msg.request())
    {
    case GET_DESCRIPTOR:
        return get_descriptor(msg);

    case GET_REPORT:
        if (auto sel = report::selector(msg.request().wValue);
            magic_enum::enum_contains<report::type>(sel.type()))
        {
            if (auto data = session_->get_report(
                    sel, std::span<uint8_t>(msg.buffer().begin(), msg.buffer().max_size()));
                !data.empty())
            {
                return msg.send_data(data);
            }
        }
        return msg.reject();

    case SET_REPORT:
        if (auto type = static_cast<report::type>(msg.request().wValue.high_byte());
            (type == report::type::FEATURE) or (type == report::type::OUTPUT))
        {
            auto& buffer = rx_buffers_[type];
            // prefer the application provided buffer
            if (buffer.size() >= msg.request().wLength)
            {
                return msg.receive_data(buffer);
            }
#if 0
            // fall back to generic control buffer
            else if (msg.buffer().max_size() >= msg.request().wLength)
            {
                return msg.receive_to_buffer();
            }
#endif
        }
        return msg.reject();

    case GET_PROTOCOL:
        return msg.send_value((session_ != nullptr) ? session_->protocol() : protocol::REPORT);

#if CONFIG_C2USB_HID_BOOT_PROTOCOL
    case SET_PROTOCOL:
        if (auto prot = static_cast<protocol>(value_lb);
            magic_enum::enum_contains(prot) and
            ((protocol_mode() != boot_protocol_mode::NONE) or (prot == protocol::REPORT)))
        {
            auto boot_prot =
                (prot == ::hid::protocol::BOOT) ? protocol_mode() : ::hid::boot::mode::NONE;
            if ((session_ == nullptr) or (session_->boot_protocol() != boot_prot))
            {
                app_base_function::start(iface, boot_prot);
            }
            return msg.confirm();
        }
        return msg.reject();
#endif

    case GET_IDLE:
        return msg.send_value(
            static_cast<uint8_t>(session_->get_idle(value_lb, idle_rate_ms_multiplier)));

    case SET_IDLE:
        return msg.set_reply(session_->set_idle(value_lb, msg.request().wValue.high_byte(),
                                                idle_rate_ms_multiplier));

    default:
        return msg.reject();
    }
}

void function::control_data_complete(message& msg, [[maybe_unused]] const config::interface& iface)
{
    using namespace standard::interface;
    using namespace hid::control;

    switch (msg.request())
    {
    case SET_REPORT:
    {
        auto type = static_cast<report::type>(msg.request().wValue.high_byte());
        rx_buffers_[type] = {};
        session_->set_report(type, msg.data().to_span());
        break;
    }
    default:
        break;
    }

    return msg.confirm();
}

} // namespace usb::df::hid
