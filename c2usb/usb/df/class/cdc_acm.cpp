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
#include "usb/df/class/cdc_acm.hpp"
#include "usb/df/message.hpp"

namespace usb::df::cdc::acm
{
void function::describe_config(const config::interface& iface, uint8_t if_index, df::buffer& buffer)
{
    if (iface.primary())
    {
        // although permitted by the standard, some OS-es (notable example is Linux)
        // don't enumerate this function without a notify endpoint present
        assert(iface.endpoints().size() == 1);

        struct acm_desc_set
        {
            usb::cdc::descriptor::call_management call_mgmt{};
            usb::cdc::descriptor::abstract_control_management acm{};
        };
        auto* iface_desc = get_base_functional_descriptors(acm::class_info(), if_index, buffer);
        auto* acm_descs = buffer.allocate<acm_desc_set>();

        acm_descs->call_mgmt.bDataInterface = if_index + 1;

        capabilities caps{};
        caps.line_control = true;
        caps.network_connection = not iface.endpoints()[0].unused();
        // TODO: add logic for the rest of the capabilities
        acm_descs->acm.bmCapabilities = caps;

        iface_desc->bNumEndpoints = describe_endpoints(iface, buffer);
        assert((iface_desc->bNumEndpoints == 1) and
               (iface.endpoints()[0].address().direction() == direction::IN));
    }
    else
    {
        auto* iface_desc = buffer.allocate<standard::descriptor::interface>();

        iface_desc << acm::data_class_info();
        iface_desc->bInterfaceNumber = if_index;
        iface_desc->bNumEndpoints = describe_endpoints(iface, buffer);
        assert((iface_desc->bNumEndpoints == 2) and
               (iface.endpoints()[0].address().direction() == direction::OUT) and
               (iface.endpoints()[1].address().direction() == direction::IN));
    }
}

void function::control_setup_request(message& msg, const config::interface& iface)
{
    if (!iface.primary())
    {
        msg.reject();
    }

    using namespace usb::cdc::control;
    switch (msg.request())
    {
    case SET_LINE_CODING:
        return msg.receive(line_coding());

    case GET_LINE_CODING:
        return msg.send(line_coding());

    case SET_CONTROL_LINE_STATE:
    {
        line_config_.bControlLineState = msg.request().wValue.low_byte();
        set_line(line_config_, line_event::STATE_CHANGE);
        return msg.confirm();
    }
    case SEND_BREAK:
        // if 0xFFFF, then break is held until 0x0000 is received
        // uint16_t break_ms = msg.request().wValue;
        // not supported yet (see capabilities)
    default:
        return msg.reject();
    }
}

void function::control_data_complete(message& msg, [[maybe_unused]] const config::interface& iface)
{
    using namespace usb::cdc::control;
    switch (msg.request())
    {
    case SET_LINE_CODING:
        if (msg.data().size() != sizeof(line_coding()))
        {
            return msg.reject();
        }
        assert(msg.data().size() == sizeof(line_coding()));
        set_line(line_config_, line_event::CODING_CHANGE);
        break;
    }

    return msg.confirm();
}

void function::enable(const config::interface& iface, uint8_t alt_sel)
{
    if (iface.primary())
    {
        open_notify_ep(iface);
    }
    else
    {
        open_data_eps(iface);
        in_ep_mps_ = iface.endpoints()[0].wMaxPacketSize;
        tx_len_ = 0;
    }
}

void function::transfer_complete(ep_handle eph, const transfer& t)
{
    if (eph == ep_out_handle())
    {
        return data_received(t);
    }
    if (eph == ep_in_handle())
    {
#if 0
        auto len = t.size();
        if (len == 0)
        {
            if (tx_len_ > 0)
            {
                // if ZLP is finished, substitute original length
                len = tx_len_;
                tx_len_ = 0;
            }
        }
        else if ((len % in_ep_mps_) == 0)
        {
            // if length mod MPS == 0, split the transfer by sending ZLP
            tx_len_ = len;
            send_ep(eph, {});
            return;
        }
        return data_sent({const_cast<const uint8_t*>(t.data()), len});
#endif
        bool needs_zlp = t.size() and (t.size() % in_ep_mps_) == 0;
        return data_sent(t, needs_zlp);
    }
    // notification sent
}

} // namespace usb::df::cdc::acm
