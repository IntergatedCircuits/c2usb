#include "cdc_helpers.hpp"

#include <algorithm>
#include <cstring>

#include <zephyr/kernel.h>
#include <zephyr/semaphore.hpp>

using namespace std::chrono_literals;
using namespace zephyr;

namespace c2usb::device_virtual
{
struct bulk_completion
{
    int error{};
    size_t length{};
};

zephyr::binary_semaphore bulk_transfer_done{0};
bulk_completion last_bulk_completion;

int bulk_transfer_callback(struct usb_device* dev, struct uhc_transfer* xfer)
{
    last_bulk_completion.error = xfer->err;
    last_bulk_completion.length = xfer->buf->len;
    bulk_transfer_done.release();
    return 0;
}

int bulk_transfer(usb::test::device* dev, uint8_t endpoint, uint8_t* data, size_t length,
                  size_t& transferred)
{
    auto* xfer = usbh_xfer_alloc(dev, endpoint, bulk_transfer_callback, nullptr);
    if (xfer == nullptr)
    {
        return -ENOMEM;
    }

    auto* buf = usbh_xfer_buf_alloc(dev, length);
    if (buf == nullptr)
    {
        usbh_xfer_free(dev, xfer);
        return -ENOMEM;
    }
    xfer->buf = buf;

    if ((endpoint & 0x80) == 0)
    {
        net_buf_add_mem(buf, data, length);
    }

    bulk_transfer_done.try_acquire();
    int err = usbh_xfer_enqueue(dev, xfer);
    if (err != 0)
    {
        usbh_xfer_buf_free(dev, buf);
        usbh_xfer_free(dev, xfer);
        return err;
    }
    if (!bulk_transfer_done.try_acquire_for(3s))
    {
        return -ETIMEDOUT;
    }

    transferred = (endpoint & 0x80) != 0 ? last_bulk_completion.length : length;
    err = last_bulk_completion.error;
    if (err == 0 and (endpoint & 0x80) != 0)
    {
        std::memcpy(data, buf->data, transferred);
    }
    usbh_xfer_buf_free(dev, buf);
    usbh_xfer_free(dev, xfer);
    return err;
}

bool configure_cdc_shell(usb::test::device* dev)
{
    return configure_cdc_interface(dev, 0);
}

bool configure_cdc_interface(usb::test::device* dev, uint8_t interface_index)
{
    auto configuration = dev->get_configuration();
    if (!configuration)
    {
        return false;
    }
    if (configuration.value() == 0 and !dev->set_configuration(1))
    {
        return false;
    }

    return set_cdc_line_state(dev, interface_index, true);
}

bool set_cdc_line_state(usb::test::device* dev, uint8_t interface_index, bool dtr)
{
    const auto request = usb::control::request{usb::cdc::control::SET_CONTROL_LINE_STATE,
                                               static_cast<uint16_t>(dtr), interface_index};
    return dev->control_out(request);
}

int send_bulk(usb::test::device* dev, std::span<const uint8_t> data)
{
    auto* out_ep = dev->ep_out[0x01].desc;
    if (out_ep == nullptr)
    {
        return -ENODEV;
    }
    const size_t max_transfer_size = USB_MPS_EP_SIZE(out_ep->wMaxPacketSize);
    if (max_transfer_size == 0)
    {
        return -EINVAL;
    }

    for (size_t offset = 0; offset < data.size();)
    {
        const size_t chunk_size = std::min(max_transfer_size, data.size() - offset);
        size_t transferred{};
        int err = bulk_transfer(dev, 0x01, const_cast<uint8_t*>(data.data() + offset), chunk_size,
                                transferred);
        if (err != 0 or transferred != chunk_size)
        {
            return err != 0 ? err : -EIO;
        }
        offset += chunk_size;
    }
    return 0;
}

int send_text(usb::test::device* dev, std::string_view text)
{
    return send_bulk(dev, {reinterpret_cast<const uint8_t*>(text.data()), text.size()});
}

size_t count_text(std::span<const uint8_t> bytes, std::string_view text)
{
    size_t count{};
    auto it = bytes.begin();
    while (it != bytes.end())
    {
        auto found = std::ranges::search(it, bytes.end(), text.begin(), text.end());
        if (found.begin() == bytes.end())
        {
            break;
        }
        ++count;
        it = found.begin() + text.size();
    }
    return count;
}

bool read_until(usb::test::device* dev, std::span<uint8_t> output, size_t& output_size,
                std::span<const uint8_t> expected, size_t marker_count, std::string_view marker,
                uint8_t endpoint)
{
    std::array<uint8_t, 64> packet{};
    for (size_t attempt = 0; attempt < 256; ++attempt)
    {
        size_t received{};
        int err = bulk_transfer(dev, endpoint, packet.data(), packet.size(), received);
        if (err != 0)
        {
            return false;
        }
        if (received > output.size() - output_size)
        {
            return false;
        }
        std::copy_n(packet.begin(), received, output.begin() + output_size);
        output_size += received;
        auto bytes = std::span<const uint8_t>{output.data(), output_size};
        auto match = std::ranges::search(bytes, expected);
        if (match.begin() != bytes.end() and count_text(bytes, marker) >= marker_count)
        {
            return true;
        }
    }
    return false;
}
} // namespace c2usb::device_virtual
