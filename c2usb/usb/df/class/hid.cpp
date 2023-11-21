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
#include <hid/report_protocol.hpp>
#include <magic_enum.hpp>

#include "usb/df/class/hid.hpp"
#include "usb/df/message.hpp"
#include "usb/standard/descriptors.hpp"

using namespace ::hid;
using namespace usb::df::hid;
using namespace usb;

void app_base_function::start(const config::interface& iface, ::hid::protocol prot)
{
    stop(iface);

    // open endpoints
    open_eps(iface.endpoints(), ephs_);
    assert(ep_in_handle().valid());

    // start application
    bool success = app_.setup<app_base_function, &app_base_function::send_report,
                              &app_base_function::receive_report>(this, prot);
    assert(success); // support for not success case requires a lot more complicated design
}

void app_base_function::stop(const config::interface& iface)
{
    if (app_.teardown(this))
    {
        close_eps(ephs_);
    }

    get_report_ = {};
}

usb::result app_base_function::send_report(const std::span<const uint8_t>& data, report::type type)
{
    if ((get_report_.type() == type) and
        ((get_report_.id() == 0) or (get_report_.id() == data.front())))
    {
        auto* msg = pending_message();
        if (msg != nullptr)
        {
            msg->send_data(data);
        }

        get_report_ = {};
        return result::OK;
    }
    else if (type == report::type::INPUT)
    {
        return send_ep(ep_in_handle(), data);
    }
    else
    {
        // feature reports can only be sent if a GET_REPORT command is pending
        // output reports cannot be sent
        return result::INVALID;
    }
}

usb::result app_base_function::receive_report(const std::span<uint8_t>& data, report::type type)
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
        return usb::result::OK;
    }
}

void app_base_function::transfer_complete(ep_handle eph, const transfer& t)
{
    if (eph == ep_in_handle())
    {
        app_.in_report_sent(t);
    }
    else
    {
        app_.set_report(report::type::OUTPUT, t);
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
    iface_desc->bInterfaceSubClass = protocol_ != boot_protocol_mode::NONE;
    iface_desc->bInterfaceProtocol = static_cast<uint8_t>(protocol_);
    iface_desc->iInterface = to_istring(0);
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
        return msg.send_data(app_.report_info().descriptor);

    default:
        return msg.reject();
    }
}

void function::control_setup_request(message& msg, const config::interface& iface)
{
    using namespace standard::interface;
    using namespace hid::control;

    auto value_lb = msg.request().wValue.low_byte();
    switch (msg.request())
    {
    case GET_DESCRIPTOR:
        return get_descriptor(msg);

    case GET_REPORT:
    {
        auto rs = report::selector(msg.request().wValue);
        if ((rs.type() != report::type::FEATURE) and (rs.type() != report::type::INPUT))
        {
            return msg.reject();
        }
        get_report_ = rs;
        app_.get_report(get_report_,
                        std::span<uint8_t>(msg.buffer().begin(), msg.buffer().max_size()));
        return; // setting the message will be done inside the application call
    }

    case SET_REPORT:
    {
        auto type = static_cast<report::type>(msg.request().wValue.high_byte());
        if ((type != report::type::FEATURE) and (type != report::type::OUTPUT))
        {
            return msg.reject();
        }
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
        else
        {
            return msg.reject();
        }
    }

    case GET_PROTOCOL:
        return msg.send_value(app_.get_protocol());

    case SET_PROTOCOL:
    {
        auto prot = static_cast<protocol>(value_lb);
        if (not magic_enum::enum_contains<protocol>(value_lb) or
            ((protocol_ == boot_protocol_mode::NONE) and (prot == protocol::BOOT)))
        {
            return msg.reject();
        }
        if (app_.get_protocol() != prot)
        {
            start(iface, prot);
        }
        return msg.confirm();
    }

    case GET_IDLE:
        return msg.send_value(static_cast<uint8_t>(app_.get_idle(value_lb) / 4));

    case SET_IDLE:
        return msg.set_reply(app_.set_idle(msg.request().wValue.high_byte() * 4, // conversion to ms
                                           value_lb));

    default:
        return msg.reject();
    }
}

void function::control_data_complete(message& msg, const config::interface& iface)
{
    using namespace standard::interface;
    using namespace hid::control;

    switch (msg.request())
    {
    case SET_REPORT:
    {
        auto type = static_cast<report::type>(msg.request().wValue.high_byte());
        rx_buffers_[type] = {};
        app_.set_report(type, msg.transferred_data());
        break;
    }

    case GET_DESCRIPTOR:
        if (static_cast<hid::descriptor::type>(msg.request().wValue.high_byte()) ==
            hid::descriptor::type::REPORT)
        {
            // start the application in default report mode
            // after the report descriptor was transferred
            start(iface, protocol::REPORT);
        }
        break;

    default:
        break;
    }

    return msg.confirm();
}

df::config::elements<2> usb::df::hid::config(function& fn, const df::config::endpoint& in_ep)
{
    assert(in_ep.address().direction() == direction::IN);
    return config::to_elements({df::config::interface{fn}, in_ep});
}

df::config::elements<2> usb::df::hid::config(function& fn, usb::speed speed,
                                             endpoint::address in_ep_addr, uint8_t in_interval)
{
    const size_t in_mps = std::min(fn.app().report_info().max_input_size,
                                   endpoint::packet_size_limit(endpoint::type::INTERRUPT, speed));
    return config(fn, config::endpoint::interrupt(in_ep_addr, in_mps, in_interval));
}

df::config::elements<3> usb::df::hid::config(function& fn, const df::config::endpoint& in_ep,
                                             const df::config::endpoint& out_ep)
{
    assert((in_ep.address().direction() == direction::IN) and
           (out_ep.address().direction() == direction::OUT));
    return config::to_elements({df::config::interface{fn}, in_ep, out_ep});
}

df::config::elements<3> usb::df::hid::config(function& fn, usb::speed speed,
                                             endpoint::address in_ep_addr, uint8_t in_interval,
                                             endpoint::address out_ep_addr, uint8_t out_interval)
{
    const size_t in_mps = std::min(fn.app().report_info().max_input_size,
                                   endpoint::packet_size_limit(endpoint::type::INTERRUPT, speed));
    const size_t out_mps = std::min(fn.app().report_info().max_output_size,
                                    endpoint::packet_size_limit(endpoint::type::INTERRUPT, speed));
    return config(fn, config::endpoint::interrupt(in_ep_addr, in_mps, in_interval),
                  config::endpoint::interrupt(out_ep_addr, out_mps, out_interval));
}
