// SPDX-License-Identifier: MPL-2.0
#include "usb/df/class/cdc_ncm.hpp"
#include "usb/df/message.hpp"

namespace usb::df::cdc::ncm
{
void function::describe_config(const config::interface& iface, uint8_t if_index,
                               df::buffer& buffer) const
{
    if (iface.primary())
    {
        assert(iface.endpoints().size() == 1);

        struct ncm_desc_set
        {
            usb::cdc::descriptor::ethernet_networking enet{};
            usb::cdc::descriptor::network_control ncm{};
        };
        auto* iface_desc = get_base_functional_descriptors(ncm::class_info(), if_index, buffer);
        auto* ncm_descs = buffer.allocate<ncm_desc_set>();

        ncm_descs->enet.iMACAddress = mac_address_string();
        // ncm_descs->enet.wMaxSegmentSize = ethernet_networking::DEFAULT_MAX_SEGMENT_SIZE;

        iface_desc->bNumEndpoints = describe_endpoints(iface, buffer);
        assert((iface_desc->bNumEndpoints == 1) and
               (iface.endpoints()[0].address().direction() == direction::IN) and
               not iface.endpoints()[0].unused());
    }
    else
    {
        // first interface descriptor is for the inactive default behavior
        auto* iface_desc = buffer.allocate<standard::descriptor::interface>();
        iface_desc << ncm::data_class_info();
        iface_desc->bInterfaceNumber = if_index;

        // function is activated by setting alternate setting 1
        iface_desc = buffer.allocate<standard::descriptor::interface>();
        iface_desc << ncm::data_class_info();
        iface_desc->bInterfaceNumber = if_index;
        iface_desc->bAlternateSetting = active_alt_setting;
        iface_desc->bNumEndpoints = describe_endpoints(iface, buffer);
        assert((iface_desc->bNumEndpoints == 2) and
               (iface.endpoints()[0].address().direction() == direction::OUT) and
               (iface.endpoints()[1].address().direction() == direction::IN));
    }
}

void function::send_string(uint8_t rel_index, string_message& smsg)
{
    if (rel_index == mac_address_string_index)
    {
        return smsg.send_as_hex_string(netif_.get_address(*this));
    }
    return named_function::send_string(rel_index, smsg);
}

uint8_t function::get_alt_setting(const config::interface& iface) const
{
    if (iface.primary())
    {
        return 0;
    }
    return uint8_t(selected_) * active_alt_setting;
}

void function::control_setup_request(message& msg, const config::interface& iface)
{
    if (!iface.primary())
    {
        msg.reject();
    }

#if 1 // TODO: test if this is needed
    notify_payload_.speed_change.wIndex = msg.request().wIndex;
    notify_payload_.connection.wIndex = msg.request().wIndex;
#endif

    using namespace usb::cdc::control;
    using namespace usb::cdc::ncm::ntb;

    switch (msg.request())
    {
    case GET_NTB_PARAMETERS:
    {
        auto* params = msg.buffer().allocate<parameters>();

        static_assert(NTB_SIZE == 16);
        params->bmNtbFormatsSupported.ntb16 = true;
        params->dwNtbInMaxSize = ntb_in_.size();
        params->wNdpInDivisor = sizeof(uint32_t);
        params->wNdpInAlignment = alignof(uint32_t);
        params->wNdpInPayloadRemainder = 0;
        params->dwNtbOutMaxSize = ntb_out_.size();
        params->wNdpOutDivisor = sizeof(uint32_t);
        params->wNdpOutAlignment = alignof(uint32_t);
        params->wNdpOutPayloadRemainder = 0;
        params->wNtbOutMaxDatagrams = ntb_out_.max_datagram_count();

        return msg.send_buffer();
    }

    case GET_NTB_INPUT_SIZE:
        return msg.send_value(ntb_in_.max_size);

    case SET_NTB_INPUT_SIZE:
        return msg.receive_to_buffer();

    default:
        return msg.reject();
    }
}

void function::control_data_complete(message& msg, [[maybe_unused]] const config::interface& iface)
{
    using namespace usb::cdc::control;
    using namespace usb::cdc::ncm;

    switch (msg.request())
    {
    case SET_NTB_INPUT_SIZE:
        // TODO: guard against race with allocate / commit datagram
        if (not ntb_in_.set_input_size(msg.data().to_const_span()))
        {
            return msg.reject();
        }
        break;
    default:
        break;
    }

    return msg.confirm();
}

void function::enable(const config::interface& iface, uint8_t alt_sel)
{
    using namespace usb::cdc::ncm::ntb;

    if (iface.primary())
    {
        open_notify_ep(iface);

        // Set initial max IN NTB size
        ntb_in_.max_size = ntb_in_.size();
    }
    else
    {
        selected_ = alt_sel == active_alt_setting;
        if (selected_)
        {
            open_data_eps(iface);

            ntb_in_.page = {};
            ntb_in_.header(ntb_in_.page)->Sequence = 0;
            ntb_in_.init();
            in_flags_.store(ntb_clear_to_send_flag);

            ntb_out_.page = {};
            out_flags_.store(ntb_no_rx_flags);

            netif_.enable(*this);
        }
    }
}

void function::disable(const config::interface& iface)
{
    if (iface.primary())
    {
    }
    else if (selected_)
    {
        selected_ = false;
        notify_payload_.connection.wValue = 0;
        // TODO: clear more flags?
        in_flags_.fetch_and(flag_type(~ntb_connected_flag));
        netif_.disable(*this);

        out_flags_.store(0);
    }
    // close endpoints
    cdc::function::disable(iface);
}

c2usb::result function::connect(uint32_t bitrate)
{
    if (not selected_)
    {
        return std::errc::connection_reset;
    }
    if (connected())
    {
        return std::errc::connection_already_in_progress;
    }
    if ((in_flags_.fetch_or(ntb_notify_flag) & ntb_notify_flag) != 0)
    {
        return std::errc::device_or_resource_busy;
    }

    // send two notifications in one go: speed change and connection
    notify_payload_.speed_change.DLBitRate = bitrate;
    notify_payload_.speed_change.ULBitRate = bitrate;
    notify_payload_.connection.wValue = 1;
    auto result = notify( // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        std::span(reinterpret_cast<const uint8_t*>(&notify_payload_), sizeof(notify_payload_)));
    if (result == c2usb::result::ok)
    {
        if (not ntb_receive(0) and not ntb_receive(1))
        {
            // failed to start receiving on either buffer
        }
    }
    else
    {
        notify_payload_.connection.wValue = 0;
        in_flags_.fetch_and(flag_type(~ntb_notify_flag));
    }
    return result;
}

c2usb::result function::disconnect()
{
    if (not selected_)
    {
        return std::errc::connection_reset;
    }
    if (not connected())
    {
        return std::errc::connection_already_in_progress;
    }
    if ((in_flags_.fetch_or(ntb_notify_flag) & ntb_notify_flag) != 0)
    {
        return std::errc::device_or_resource_busy;
    }

    notify_payload_.connection.wValue = 0;
    auto result = notify(notify_payload_.connection);
    if (result == c2usb::result::ok)
    {
    }
    else
    {
        notify_payload_.connection.wValue = 1;
        in_flags_.fetch_and(flag_type(~ntb_notify_flag));
    }
    return result;
}

c2usb::result function::allocate_datagram(size_t size, std::span<uint8_t>& dg)
{
    if (size < usb::cdc::ncm::DATAGRAM_MIN_LENGTH)
    {
        return std::errc::invalid_argument;
    }
    if (not selected_ or not connected())
    {
        return std::errc::not_connected;
    }
    if (int(size) > ntb_in_.max_datagram_size())
    {
        return std::errc::not_enough_memory;
    }
    dg = ntb_in_.allocate_datagram(size);
    if (dg.empty())
    {
        return std::errc::resource_unavailable_try_again;
    }
    return c2usb::result::ok;
}

c2usb::result function::commit_datagram(const std::span<uint8_t>& dg)
{
    // protect integrity from TX ISR, and indicate that there is data to send
   [[maybe_unused]] auto flags = in_flags_.fetch_or(ntb_manipulate_flag | ntb_pending_flag);
    assert((flags & ntb_manipulate_flag) == 0);

    ntb_in_.commit_datagram(dg);

    // clear manipulate flag
    flags = in_flags_.fetch_and(flag_type(~(ntb_manipulate_flag)));

    // when clear to send, any TX transfer has been completed
    if ((flags & (ntb_clear_to_send_flag | ntb_connected_flag)) ==
        (ntb_clear_to_send_flag | ntb_connected_flag))
    {
        [[maybe_unused]] auto result = try_send();
    }

    // TODO: return value needs reconsideration
    return c2usb::result::ok;
}

c2usb::result function::try_send(flag_type flags_to_clear)
{
    if (ntb2send_.empty())
    {
        // new NTB is popped, so no more pending data
        ntb2send_ = ntb_in_.pop_ntb();
        flags_to_clear |= ntb_pending_flag;
    }
    auto result = send_data(ntb2send_);
    if (result == c2usb::result::ok)
    {
        flags_to_clear |= ntb_clear_to_send_flag;
    }
    else
    {
        flags_to_clear &= flag_type(~ntb_pending_flag);
    }
    in_flags_.fetch_and(flag_type(~flags_to_clear));
    return result;
}

void function::ep_in_callback(const transfer& t)
{
    flag_type flags_to_set = 0;
    // update the appropriate flags
    if (t.endpoint() == ep_notify_handle())
    {
        if (t.success())
        {
            [[maybe_unused]] auto flags = in_flags_.fetch_xor(ntb_connected_flag | ntb_notify_flag);
            assert(((flags & ntb_notify_flag) != 0) and
                   (((flags & ntb_connected_flag) == 0) == connected()));
        }
        else
        {
            // revert connection state if notification failed
            notify_payload_.connection.wValue = uint16_t(not connected());
            [[maybe_unused]] auto flags = in_flags_.fetch_xor(ntb_notify_flag);
            assert((flags & ntb_notify_flag) != 0);
        }
    }
    else // (t.endpoint() == ep_in_handle())
    {
        flags_to_set = ntb_clear_to_send_flag;
        if (t.success())
        {
            ntb2send_ = {};
        }
    }

    // when the conditions are right, send the next NTB IN
    auto flags = in_flags_.fetch_or(flags_to_set);
    if (flags == (ntb_pending_flag | ntb_connected_flag))
    {
        bool new_ntb = ntb2send_.empty();
        auto result = try_send();
        if ((result == c2usb::result::ok) and new_ntb)
        {
            netif_.tx_buffer_available(*this);
        }
    }
    // send ZLP when necessary otherwise
    else if ((t.endpoint() == ep_in_handle()) and (t.size() < ntb_in_.max_size) and
             t.needs_zlp(in_ep_mps()))
    {
        if (auto result = send_data({}); result == c2usb::result::ok)
        {
            in_flags_.fetch_and(flag_type(~ntb_clear_to_send_flag));
        }
    }
}

bool function::pop_datagram_from(page_type page, std::span<const uint8_t>& dg)
{
    auto flags = out_flags_.fetch_or(flags_by_page(ntb_manipulate_flag, page));
    if (flags == flags_by_page(ntb_pending_flag, page))
    {
        if (not usb::cdc::ncm::is_valid_ntb<NTB_SIZE>(ntb_out_.span(page)))
        {
            // invalid NTB, clear the pending flag for this page
            out_flags_.fetch_and(flag_type(~flags_by_page(ntb_pending_flag, page)));
            return false;
        }

        // first time accessing this NTB, set the header pointer to the beginning of the buffer
        ntb_out_.page = page;
        ntb_out_.set_header(ntb_out_.data(page));
    }
    const auto processing_flags = flags_by_page(ntb_pending_flag | ntb_manipulate_flag, page);
    if ((flags & processing_flags) != 0)
    {
        // see if there is an unconsumed datagram available in the NTB
        dg = ntb_out_.pop_datagram();
        // TODO: add capability to check if the NTB is fully processed at the last datagram,
        // to speed up reception
        if (dg.empty())
        {
            // NTB is fully processed or malformed
            out_flags_.fetch_and(flag_type(~processing_flags));
        }
        return true;
    }
    return false;
}

std::span<const uint8_t> function::pop_datagram()
{
    // TODO: add protection against concurrent calls to this function
    auto page = ntb_out_.page;

    std::span<const uint8_t> dg{};
    if (pop_datagram_from(page, dg) and not dg.empty())
    {
        return dg;
    }

    // switch to other NTB if available, and try to receive to an empty NTB
    if (pop_datagram_from(other(page), dg) or
        (out_flags_.load() & ntb_no_rx_flags) == ntb_no_rx_flags)
    {
        // Receive to empty NTB
        ntb_receive(page);
    }
    return dg;
}

bool function::ntb_receive(page_type page)
{
    const flag_type rx_flags = flags_by_page(ntb_ready_to_rx_flag, page);
    c2usb::result result = std::errc::device_or_resource_busy;

    if (selected_ and connected() and (out_flags_.fetch_or(rx_flags) & rx_flags) == 0)
    {
        result = receive_data({ntb_out_.data(page), ntb_out_.size()});
        if (result != c2usb::result::ok)
        {
            out_flags_.fetch_and(flag_type(~rx_flags));
        }
    }
    return result == c2usb::result::ok;
}

void function::ep_out_callback(const transfer& t)
{
    auto page = page_type(t.data() == ntb_out_.data(1));
    out_flags_.fetch_or(flags_by_page(ntb_ready_to_rx_flag, page));

    if (not selected_ or not connected())
    {
        return;
    }
    if (t.success())
    {
        ntb_out_.length[page] = t.size();
        auto flags = out_flags_.fetch_or(flags_by_page(ntb_pending_flag, page));

        // if the previous buffer is processed, pass the newly received
        if ((flags & flags_by_page(ntb_pending_flag, other(page))) == 0)
        {
            ntb_out_.page = page;
            // switch to the other page for receiving
            ntb_receive(other(page));
        }

        netif_.data_received(*this);
    }
    else
    {
        // repeat receiving
        ntb_receive(page);
    }
}

} // namespace usb::df::cdc::ncm
