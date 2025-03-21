/// @file
///
/// @author Benedek Kupper
/// @date   2024
///
/// @copyright
///         This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
///         If a copy of the MPL was not distributed with this file, You can obtain one at
///         https://mozilla.org/MPL/2.0/.
///
#ifndef __PORT_ZEPHYR_UDC_MAC_HPP_
#define __PORT_ZEPHYR_UDC_MAC_HPP_

#define C2USB_HAS_ZEPHYR_HEADERS    (__has_include("zephyr/drivers/usb/udc.h") and \
                                     __has_include("zephyr/device.h") and CONFIG_C2USB_UDC_MAC)
#if C2USB_HAS_ZEPHYR_HEADERS

#include "etl/delegate.h"
#include "usb/df/ep_flags.hpp"
#include "usb/df/mac.hpp"

struct device;
struct net_buf;
struct udc_buf_info;
struct udc_event;

int udc_mac_preinit();

namespace usb::zephyr
{
/// @brief  The udc_mac implements the MAC interface to the Zephyr next USB device stack.
class udc_mac : public df::mac
{
    friend int ::udc_mac_preinit();

  public:
    udc_mac(const ::device* dev);
    ~udc_mac() override;

    /// @brief Queues a task to be executed in the USB thread context.
    /// @param task the callable to execute
    /// @return OK if the task was queued, -ENOMSG if the queue is full
    static usb::result queue_task(etl::delegate<void()> task);

    const ::device* driver_device() const { return dev_; }

  private:
    const ::device* dev_;
    usb::df::ep_flags stall_flags_{};
    usb::df::ep_flags busy_flags_{};
    std::span<::net_buf*> ep_bufs_{};

    static void worker(void*, void*, void*);
    static int event_callback(const ::udc_event& event);
    bool ctrl_buf_valid(::net_buf* buf);
    void ctrl_stall(::net_buf* buf, int err = 0);
    ::net_buf* ctrl_buffer_allocate(::net_buf* buf);
    bool set_attached(bool attached) override;
    void allocate_endpoints(usb::df::config::view config) override;
    usb::df::ep_handle ep_address_to_handle(endpoint::address addr) const override;
    endpoint::address ep_handle_to_address(usb::df::ep_handle eph) const;
    usb::df::ep_handle ep_config_to_handle(const usb::df::config::endpoint& ep) const override;
    ::net_buf* const& ep_handle_to_buf(usb::df::ep_handle eph) const;

    void init(const usb::speeds& speeds) override;
    void deinit() override;
    usb::result signal_remote_wakeup() override;
    usb::speed speed() const override;

    static ::net_buf* move_data_out(::net_buf* buf, usb::df::transfer t);
    void process_ctrl_ep_event(::net_buf* buf, const ::udc_buf_info& info);

    static usb::result post_event(const ::udc_event& event);
    int process_event(const ::udc_event& event);
    void process_ep_event(::net_buf* buf);

    usb::result ep_set_stall(endpoint::address addr);
    usb::result ep_clear_stall(endpoint::address addr);
    usb::result ep_transfer(usb::df::ep_handle eph, const usb::df::transfer& t, usb::direction dir);

    usb::df::ep_handle ep_open(const usb::df::config::endpoint& ep) override;
    usb::result ep_send(usb::df::ep_handle eph, const std::span<const uint8_t>& data) override;
    usb::result ep_receive(usb::df::ep_handle eph, const std::span<uint8_t>& data) override;
    usb::result ep_close(usb::df::ep_handle eph) override;
    usb::result ep_cancel(usb::df::ep_handle eph);
    bool ep_is_stalled(usb::df::ep_handle eph) const override;
    usb::result ep_change_stall(usb::df::ep_handle eph, bool stall) override;
};

} // namespace usb::zephyr

#endif // C2USB_HAS_ZEPHYR_HEADERS

#endif // __PORT_ZEPHYR_UDC_MAC_HPP_
