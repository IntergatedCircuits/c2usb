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
                                     __has_include("zephyr/device.h"))
#if C2USB_HAS_ZEPHYR_HEADERS

#include "etl/delegate.h"
#include "usb/df/ep_flags.hpp"
#include "usb/df/mac.hpp"

extern "C"
{
#include <zephyr/drivers/usb/udc.h>
}

namespace usb::zephyr
{
/// @brief  The udc_mac implements the MAC interface to the Zephyr next USB device stack.
class udc_mac : public df::mac
{
    static constexpr udc_event_type UDC_MAC_TASK = (udc_event_type)-1;

  public:
    udc_mac(const ::device* dev);
    ~udc_mac() override;

    /// @brief Queues a task to be executed in the USB thread context.
    /// @param task the callable to execute
    /// @return OK if the task was queued, -ENOMSG if the queue is full
    static usb::result queue_task(etl::delegate<void()> task)
    {
        udc_event event{.type = UDC_MAC_TASK};
        static_assert(offsetof(udc_event, type) == 0 and
                      sizeof(task) <= (sizeof(event) - offsetof(udc_event, value)));
        std::memcpy(&event.value, &task, sizeof(task));
        return post_event(event);
    }

    static int event_callback(const device* dev, const udc_event* event);

  private:
    const ::device* dev_;
    ::net_buf* ctrl_buf_{};
    usb::df::ep_flags stall_flags_{};
    usb::df::ep_flags busy_flags_{};
    std::span<::net_buf*> ep_bufs_{};

    void allocate_endpoints(usb::df::config::view config) override;
    usb::df::ep_handle ep_address_to_handle(endpoint::address addr) const override;
    const usb::df::config::endpoint& ep_handle_to_config(usb::df::ep_handle eph) const override;
    endpoint::address ep_handle_to_address(usb::df::ep_handle eph) const override;
    usb::df::ep_handle ep_config_to_handle(const usb::df::config::endpoint& ep) const override;
    ::net_buf* const& ep_handle_to_buf(usb::df::ep_handle eph) const;

    static udc_mac* lookup(const device* dev);

    void init(const usb::speeds& speeds) override;
    void deinit() override;
    bool set_attached(bool attached) override;
    void signal_remote_wakeup() override;
    usb::speed speed() const override;

    void control_ep_open() override;
    void move_data_out(usb::df::transfer t);
    void control_reply(usb::direction dir, const usb::df::transfer& t) override;
    void process_ctrl_ep_event(net_buf* buf, const udc_buf_info& info);

    static usb::result post_event(const udc_event& event);
    int process_event(const udc_event& event);
    void process_ep_event(net_buf* buf);

    usb::result ep_set_stall(endpoint::address addr);
    usb::result ep_clear_stall(endpoint::address addr);
    usb::result ep_transfer(usb::df::ep_handle eph, const usb::df::transfer& t, usb::direction dir);

    void cancel_all_transfers();
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
