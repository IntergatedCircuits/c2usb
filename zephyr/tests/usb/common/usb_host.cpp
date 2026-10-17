#include "usb_host.hpp"
#include <cstring>
#include <zephyr/semaphore.hpp>

namespace usb::test
{
namespace
{
struct bulk_completion
{
    int error{};
    size_t length{};
};

zephyr::binary_semaphore bulk_transfer_done{0};
bulk_completion last_bulk_completion;

int bulk_transfer_callback(struct usb_device*, struct uhc_transfer* xfer)
{
    last_bulk_completion.error = xfer->err;
    last_bulk_completion.length = xfer->buf->len;
    bulk_transfer_done.release();
    return 0;
}

int transfer(device* dev, uint8_t endpoint, uint8_t* data, size_t length, size_t& transferred,
             std::chrono::milliseconds timeout)
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

    while (bulk_transfer_done.try_acquire())
    {
    }
    int err = usbh_xfer_enqueue(dev, xfer);
    if (err != 0)
    {
        usbh_xfer_buf_free(dev, buf);
        usbh_xfer_free(dev, xfer);
        return err;
    }
    if (!bulk_transfer_done.try_acquire_for(timeout))
    {
        return -ETIMEDOUT;
    }

    transferred = (endpoint & 0x80) != 0 ? last_bulk_completion.length : length;
    err = last_bulk_completion.error;
    if ((err == 0) and ((endpoint & 0x80) != 0))
    {
        std::memcpy(data, buf->data, transferred);
    }
    usbh_xfer_buf_free(dev, buf);
    usbh_xfer_free(dev, xfer);
    return err;
}
} // namespace

int bulk_transfer(device* dev, uint8_t endpoint, std::span<const uint8_t> data, size_t& transferred,
                  std::chrono::milliseconds timeout)
{
    if ((endpoint & 0x80) != 0)
    {
        return -EINVAL;
    }
    return transfer(dev, endpoint, const_cast<uint8_t*>(data.data()), data.size(), transferred,
                    timeout);
}

int bulk_transfer(device* dev, uint8_t endpoint, std::span<uint8_t> data, size_t& transferred,
                  std::chrono::milliseconds timeout)
{
    return transfer(dev, endpoint, data.data(), data.size(), transferred, timeout);
}
} // namespace usb::test
