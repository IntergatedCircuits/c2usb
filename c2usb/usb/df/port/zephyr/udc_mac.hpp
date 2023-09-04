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
#ifndef __USB_DF_PORT_ZEPHYR_UDC_MAC_HPP_
#define __USB_DF_PORT_ZEPHYR_UDC_MAC_HPP_

#define C2USB_HAS_ZEPHYR_HEADERS    (__has_include("zephyr/drivers/usb/udc.h") and \
                                     __has_include("zephyr/device.h"))
#if C2USB_HAS_ZEPHYR_HEADERS

#include "usb/df/mac.hpp"
#include "usb/df/ep_flags.hpp"
extern "C"
{
#include <zephyr/drivers/usb/udc.h>
}

namespace usb::df::zephyr
{
    /// @brief  The udc_mac implements the MAC interface to the Zephyr next USB device stack.
    class udc_mac : public mac
    {
    public:
        udc_mac(const ::device* dev);
        ~udc_mac() override;

        static int event_callback(const device* dev, const udc_event* event);

    private:
        static udc_mac* list_head;
        const ::device* dev_;
        udc_mac* list_next_ {};
        ::net_buf* ctrl_buf_ {};
        usb::df::ep_flags stall_flags_ {};
        usb::df::ep_flags busy_flags_ {};
        std::span<::net_buf*> ep_bufs_ {};

        void allocate_endpoints(config::view config) override;
        ep_handle ep_address_to_handle(endpoint::address addr) const override;
        const config::endpoint& ep_handle_to_config(ep_handle eph) const override;
        endpoint::address ep_handle_to_address(ep_handle eph) const override;
        ep_handle ep_config_to_handle(const config::endpoint& ep) const override;
        ::net_buf* const& ep_handle_to_buf(ep_handle eph) const;
        static void buf_load_data(::net_buf* buf, const transfer& t);

        static udc_mac* lookup(const device* dev);

        void init(const usb::speeds& speeds) override;
        void deinit() override;
        void soft_attach() override;
        void soft_detach() override;
        void signal_remote_wakeup() override;
        usb::speed speed() const override;

        void control_ep_open() override;
        void control_transfer();
        void control_reply(usb::direction dir, const usb::df::transfer& t) override;

        int process_event(const udc_event& event);
        void process_ep_event(net_buf* buf);

        usb::result ep_set_stall(endpoint::address addr);
        usb::result ep_clear_stall(endpoint::address addr);
        usb::result ep_transfer(usb::df::ep_handle eph, const transfer& t);

        usb::df::ep_handle ep_open(const usb::df::config::endpoint& ep) override;
        usb::result ep_send(usb::df::ep_handle eph, const std::span<const uint8_t>& data) override;
        usb::result ep_receive(usb::df::ep_handle eph, const std::span<uint8_t>& data) override;
        usb::result ep_close(usb::df::ep_handle eph) override;
        usb::result ep_cancel(usb::df::ep_handle eph);
        bool ep_is_stalled(usb::df::ep_handle eph) const override;
        usb::result ep_change_stall(usb::df::ep_handle eph, bool stall) override;
    };
}

#endif // C2USB_HAS_ZEPHYR_HEADERS

#endif // __USB_DF_PORT_ZEPHYR_UDC_MAC_HPP_
