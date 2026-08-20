#pragma once

extern "C"
{
#define class class_
#include <usbh_ch9.h>
#include <usbh_device.h>
#include <zephyr/usb/usbh.h>
#undef class
}
#include <zephyr/ztest.h>

#include <chrono>
#include <usb/control.hpp>
#include <zephyr/thread.hpp>

template <typename T>
struct buf_data
{
    ::net_buf* buf{};
    ::usb_device* owner{};

    ~buf_data()
    {
        if ((buf != nullptr) and (owner != nullptr))
        {
            usbh_xfer_buf_free(owner, buf);
        }
    }

    T* operator->()
    {
        if (buf->len < sizeof(T))
        {
            return nullptr;
        }
        return reinterpret_cast<T*>(buf->data);
    }
    T* operator*()
    {
        if (buf->len < sizeof(T))
        {
            return nullptr;
        }
        return reinterpret_cast<T*>(buf->data);
    }
    bool has_data() const { return (buf != nullptr) and (buf->len >= sizeof(T)); }
    bool exact_size() const { return (buf != nullptr) and (buf->len == sizeof(T)); }

    std::span<const uint8_t> as_span() const
    {
        if (buf == nullptr)
        {
            return {};
        }
        return {buf->data, buf->len};
    }

    operator bool() const { return (buf != nullptr) and (buf->len >= sizeof(T)); }
};

namespace usb::test
{
struct device : ::usb_device
{
    int lock(std::chrono::milliseconds timeout = std::chrono::milliseconds(200))
    {
        int err = k_mutex_lock(&mutex, K_MSEC(timeout.count()));
        zassert_equal(err, 0, "Failed to lock USB host device");
        return err;
    }
    int unlock()
    {
        int err = k_mutex_unlock(&mutex);
        zassert_equal(err, 0, "Failed to unlock USB host device");
        return err;
    }

    std::optional<uint8_t> get_configuration()
    {
        uint8_t cfg = 0;
        lock();
        int err = usbh_req_get_cfg(this, &cfg);
        unlock();
        if (err != 0)
        {
            return std::nullopt;
        }
        return cfg;
    }
    bool set_configuration(uint8_t cfg)
    {
        lock();
        int err = usbh_req_set_cfg(this, cfg);
        unlock();
        return err == 0;
    }
    bool control_out(const usb::control::request& req)
    {
        lock();
        int err = usbh_req_setup(this, req.bmRequestType, req.bRequest, req.wValue, req.wIndex,
                                 req.wLength, nullptr);
        unlock();
        return err == 0;
    }
    template <typename T>
    bool control_out(const usb::control::request& req, const T& data)
    {
        auto* buf = usbh_xfer_buf_alloc(this, sizeof(T));
        lock();
        if (buf != nullptr)
        {
            memcpy(buf->data, &data, sizeof(T));
            buf->len = sizeof(T);
            buf->size = sizeof(T);
            int err = usbh_req_setup(this, req.bmRequestType, req.bRequest, req.wValue, req.wIndex,
                                     req.wLength, buf);
            usbh_xfer_buf_free(this, buf);
            if (err != 0)
            {
                buf = nullptr;
            }
        }
        unlock();
        return buf != nullptr;
    }

    template <typename T>
    buf_data<const T> control_in(const usb::control::request& req, size_t length)
    {
        auto* buf = usbh_xfer_buf_alloc(this, length);
        lock();
        if (buf != nullptr)
        {
            int err = usbh_req_setup(this, req.bmRequestType, req.bRequest, req.wValue, req.wIndex,
                                     req.wLength, buf);
            if (err != 0)
            {
                buf = nullptr;
            }
        }
        unlock();
        return {buf, this};
    }

    template <typename T>
    buf_data<const T> control_in(const usb::control::request& req)
    {
        return control_in<const T>(req, req.wLength);
    }
};

struct host : ::usbh_context
{
    static int start()
    {
        if (int err = usbh_init(instance()); err != 0)
        {
            return err;
        }
        return usbh_enable(instance());
    }
    static int stop()
    {
        if (int err = usbh_disable(instance()); err != 0)
        {
            return err;
        }
        return usbh_shutdown(instance());
    }
    static int bus_reset() { return uhc_bus_reset(instance()->dev); }
    static int bus_resume() { return uhc_bus_resume(instance()->dev); }
    static int sof_enable() { return uhc_sof_enable(instance()->dev); }

    static device*
    wait_for_device(std::chrono::milliseconds timeout = std::chrono::milliseconds(2000))
    {
        using namespace ::zephyr;
        for (auto start = tick_timer::now(); tick_timer::now() - start < timeout;)
        {
            if (usb_device* udev = usbh_device_get_any(instance()); udev != nullptr)
            {
                return static_cast<device*>(udev);
            }

            this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        return nullptr;
    }

  private:
    static ::usbh_context* instance()
    {
        USBH_CONTROLLER_DEFINE(uhs_ctx, DEVICE_DT_GET(DT_NODELABEL(zephyr_uhc0)));
        return &uhs_ctx;
    }
};
} // namespace usb::test
