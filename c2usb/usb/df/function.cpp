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
#include "usb/df/function.hpp"
#include "usb/df/mac.hpp"

using namespace usb::df;

void function::free_string_index()
{
    istr_base_ = 0;
}

void function::allocate_string_index(istring* pindex)
{
    if ((istr_base_ == 0) and (istr_count_ > 0))
    {
        istr_base_ = *pindex;
        *pindex += istr_count_;
    }
}

bool function::send_owned_string(istring index, string_message& smsg)
{
    if ((istr_base_ <= index) and (index < (istr_base_ + istr_count_)))
    {
        send_string(index - istr_base_, smsg);
        return true;
    }
    else
    {
        return false;
    }
}

void function::control_setup_request(message& msg, [[maybe_unused]] const config::interface& iface)
{
    return msg.reject();
}

void function::control_data_complete(message& msg, [[maybe_unused]] const config::interface& iface)
{
    return msg.confirm();
}

void function::send_string([[maybe_unused]] uint8_t rel_index, string_message& smsg)
{
    return smsg.reject();
}

void function::handle_control_setup(message& msg, const config::interface& iface)
{
    using namespace standard::interface;

    switch (msg.request())
    {
    case GET_INTERFACE:
    {
        return msg.send_value(get_alt_setting(iface));
    }

    case SET_INTERFACE:
    {
        uint8_t alt_selector = msg.request().wValue.low_byte();

        if (alt_selector < iface.alt_setting_count())
        {
            if (alt_selector != get_alt_setting(iface))
            {
                restart(iface, alt_selector);
            }
            return msg.confirm();
        }
        else
        {
            return msg.reject();
        }
    }

#if C2USB_FUNCTION_SUSPEND
    case SET_FEATURE:
    case CLEAR_FEATURE:
    {
        if ((iface.primary()) and (msg.request().wValue == feature::FUNCTION_SUSPEND))
        {
            // TODO
            return msg.confirm();
        }
        else
        {
            return msg.reject();
        }
    }

    case GET_STATUS:
    {
        auto status = std_status_;
        if (not iface.primary())
        {
            status = {};
        }
        return msg.send_value(status);
    }
#endif

    default:
        return control_setup_request(msg, iface);
    }
}

void function::handle_control_setup(message& msg, ep_handle eph)
{
    using namespace standard::endpoint;
    status ep_status{};

    switch (msg.request())
    {
    case SET_FEATURE:
    case CLEAR_FEATURE:
        if (msg.request().wValue == feature::HALT)
        {
            ep_status.halt = msg.request() == SET_FEATURE;
            if (control_endpoint_state(eph, ep_status) and
                (stall_ep(eph, ep_status.halt) == result::OK))
            {
                return msg.confirm();
            }
        }
        return msg.reject();

    case GET_STATUS:
        ep_status.halt = mac_->ep_is_stalled(eph);
        return msg.send_value(ep_status);

    default:
        return msg.reject();
    }
}

void function::handle_control_data(message& msg, const config::interface& iface)
{
    return control_data_complete(msg, iface);
}

void function::init(const config::interface& iface, mac* m)
{
    mac_ = m;
#if C2USB_FUNCTION_SUSPEND
    std_status_.remote_wakeup = false;
#endif
    start(iface, 0);
}

void function::deinit(const config::interface& iface)
{
    stop(iface);
    if (iface.primary())
    {
        mac_ = nullptr;
    }
}

ep_handle function::open_ep(const config::endpoint& ep)
{
    assert(&ep.interface().function() == this);
    if ((mac_ != nullptr) and not ep.unused())
    {
        return mac_->ep_open(ep);
    }
    else
    {
        return {};
    }
}

void function::close_ep(ep_handle& eph)
{
    if ((mac_ != nullptr) and eph.valid())
    {
        mac_->ep_close(eph);
    }
    eph = {};
}

usb::result function::send_ep(ep_handle eph, const std::span<const uint8_t>& data)
{
    if ((mac_ != nullptr) and eph.valid())
    {
        return mac_->ep_send(eph, data);
    }
    else
    {
        return usb::result::NO_TRANSPORT;
    }
}

usb::result function::receive_ep(ep_handle eph, const std::span<uint8_t>& data)
{
    if ((mac_ != nullptr) and eph.valid())
    {
        return mac_->ep_receive(eph, data);
    }
    else
    {
        return usb::result::NO_TRANSPORT;
    }
}

usb::result function::stall_ep(ep_handle eph, bool stall)
{
    if ((mac_ != nullptr) and eph.valid())
    {
        return mac_->ep_change_stall(eph, stall);
    }
    else
    {
        return usb::result::NO_TRANSPORT;
    }
}

message* function::pending_message()
{
    if (mac_ != nullptr)
    {
        return mac_->get_pending_message(this);
    }
    else
    {
        return nullptr;
    }
}

uint8_t function::describe_endpoints(const config::interface& iface, df::buffer& buffer)
{
    uint8_t count = 0;
    for (auto& ep : iface.endpoints())
    {
        buffer.append(static_cast<const standard::descriptor::endpoint&>(ep));
        count++;
    }
    return count;
}

void named_function::send_string([[maybe_unused]] uint8_t rel_index, string_message& smsg)
{
    return smsg.send_string(name_);
}
