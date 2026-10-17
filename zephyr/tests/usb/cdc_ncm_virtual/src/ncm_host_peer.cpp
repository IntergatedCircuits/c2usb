#include "ncm_host_peer.hpp"

ncm_host_peer::ncm_host_peer(usb::test::device* dev)
    : dev_(dev), tx_(tx_buffer_), rx_(rx_buffer_)
{
    tx_.max_size = tx_.size();
    tx_.init();
}

bool ncm_host_peer::queue_datagram(std::span<const uint8_t> datagram)
{
    return tx_.append_datagram(datagram.begin(), datagram.end());
}

int ncm_host_peer::transmit()
{
    auto ntb = tx_.pop_ntb();
    if (ntb.empty())
    {
        return -EINVAL;
    }
    return transmit_ntb(ntb);
}

int ncm_host_peer::transmit_malformed(std::span<const uint8_t> datagram)
{
    if (not queue_datagram(datagram))
    {
        return -ENOSPC;
    }
    auto ntb = tx_.pop_ntb();
    if (ntb.empty())
    {
        return -EINVAL;
    }
    const_cast<uint8_t*>(ntb.data())[0] ^= 0xff;
    return transmit_ntb(ntb);
}

int ncm_host_peer::transmit_ntb(std::span<const uint8_t> ntb)
{
    auto* endpoint = dev_->ep_out[0x02].desc;
    if (endpoint == nullptr)
    {
        return -ENODEV;
    }

    const size_t max_packet_size = USB_MPS_EP_SIZE(endpoint->wMaxPacketSize);
    if (max_packet_size == 0)
    {
        return -EINVAL;
    }

    for (size_t offset = 0; offset < ntb.size();)
    {
        const size_t chunk_size = std::min(max_packet_size, ntb.size() - offset);
        size_t transferred{};
        int err =
            usb::test::bulk_transfer(dev_, 0x02, ntb.subspan(offset, chunk_size), transferred);
        if ((err != 0) or (transferred != chunk_size))
        {
            return err != 0 ? err : -EIO;
        }
        offset += chunk_size;
    }
    return 0;
}

int ncm_host_peer::receive(std::span<std::span<const uint8_t>> datagrams, size_t& datagram_count,
                           std::chrono::milliseconds timeout, size_t* ntb_size)
{
    rx_.page = 0;
    auto buffer = std::span<uint8_t>{rx_.data(0), rx_.size()};
    size_t transferred{};
    int err = usb::test::bulk_transfer(dev_, 0x82, buffer, transferred, timeout);
    if (err != 0)
    {
        return err;
    }
    if ((transferred == 0) or (transferred > buffer.size()))
    {
        return -EBADMSG;
    }
    if (ntb_size != nullptr)
    {
        *ntb_size = transferred;
    }

    rx_.length[0] = transferred;
    auto ntb = std::span<const uint8_t>{buffer.data(), transferred};
    if (not usb::cdc::ncm::is_valid_ntb<16>(ntb))
    {
        return -EBADMSG;
    }
    rx_.set_header(buffer.data());

    datagram_count = 0;
    while (true)
    {
        auto datagram = rx_.pop_datagram();
        if (datagram.empty())
        {
            break;
        }
        if (datagram_count == datagrams.size())
        {
            return -ENOSPC;
        }
        datagrams[datagram_count++] = datagram;
    }
    return datagram_count == 0 ? -EBADMSG : 0;
}

int ncm_host_peer::receive_notification(std::span<uint8_t> data, size_t& transferred,
                                        std::chrono::milliseconds timeout)
{
    return usb::test::bulk_transfer(dev_, 0x81, data, transferred, timeout);
}
