#include "usb/df/class/cdc_acm.hpp"
#include "usb/df/message.hpp"

using namespace usb::df::cdc::acm;

void function::describe_config(const config::interface& iface, uint8_t if_index, df::buffer& buffer)
{
    if (iface.primary())
    {
        // although permitted by the standard, some OS-es (notable example is Linux)
        // don't enumerate this function without a notify endpoint present
        assert(iface.endpoints().size() == 1);

        struct acm_desc_set
        {
            usb::cdc::descriptor::call_management call_mgmt {};
            usb::cdc::descriptor::abstract_control_management acm {};
        };
        auto* iface_desc = get_base_functional_descriptors(acm::class_info(), if_index, buffer);
        auto* acm_descs = buffer.allocate<acm_desc_set>();

        acm_descs->call_mgmt.bDataInterface = if_index + 1;

        capabilities caps {};
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
            // TODO: deinit
            return msg.receive(line_coding_);

        case GET_LINE_CODING:
            return msg.send(line_coding_);

        case SET_CONTROL_LINE_STATE:
        {
            bool data_terminal_ready = msg.request().wValue & 1;
            bool request_to_send = msg.request().wValue & 2;
            // TODO: forward to application
            return msg.confirm();
        }
        case SEND_BREAK:
        {
            // if 0xFFFF, then break is held until 0x0000 is received
            //uint16_t break_ms = msg.request().wValue;
            // not supported yet (see capabilities)
            return msg.reject();
        }

        default:
            return msg.reject();
    }
}

void function::control_data_complete(message& msg, const config::interface& iface)
{
    using namespace usb::cdc::control;
    switch (msg.request())
    {
        case SET_LINE_CODING:
            // TODO: init
            break;
    }

    return msg.confirm();
}

void function::start(const config::interface& iface, uint8_t alt_sel)
{
    if (!iface.primary())
    {
        in_ep_mps_ = iface.endpoints()[0].wMaxPacketSize;
        tx_len_ = 0;
        open_eps(iface.endpoints(), data_ephs_);
    }
    cdc::function::start(iface, alt_sel);
}

void function::transfer_complete(ep_handle eph, const transfer& t)
{
    if (eph == ep_out_handle())
    {
        // TODO receive callback
    }
    else if (eph == ep_in_handle())
    {
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

        // TODO sent callback
    }
    else
    {
        // notification sent
    }
}
