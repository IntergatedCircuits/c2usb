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
#ifndef __USB_DF_PORT_NXP_USB_MAC_HPP_
#define __USB_DF_PORT_NXP_USB_MAC_HPP_

#define C2USB_HAS_KSDK_HEADERS  (__has_include("usb.h") and \
                                 __has_include("usb_device_config.h") and \
                                 __has_include("usb_device.h") and \
                                 __has_include("usb_device_dci.h") and \
                                 __has_include("fsl_common.h"))
#if C2USB_HAS_KSDK_HEADERS
#include "usb/df/mac.hpp"
#include <atomic>

extern "C"
{
#include "fsl_common.h"
#include "usb.h"
#include "usb_device_config.h"
#include "usb_device.h"
#include "usb_device_dci.h"
}

namespace usb::df::nxp
{
    /// @brief  The kusb_mac implements the MAC interface to the NXP USB device stack.
    class kusb_mac : public df::address_handle_mac
    {
    public:
        kusb_mac(usb_controller_index_t kusb_id)
                : usb::df::address_handle_mac(), kusb_if_(*kusb_index_to_if(kusb_id)), kusb_id_(kusb_id)
        {}

        void process_kusb_notification(const usb_device_callback_message_struct_t& message);

        void handle_irq();

    private:
        struct controller_interface : public ::_usb_device_controller_interface_struct
        {
            void (*isr)(void* param);
        };

        void* kusb_handle_ { nullptr };
        const controller_interface& kusb_if_;
        usb_controller_index_t kusb_id_;
#ifdef USB_DEVICE_CONFIG_ENDPOINTS
        static constexpr size_t MAX_EP_COUNT = USB_DEVICE_CONFIG_ENDPOINTS;
#else
        static constexpr size_t MAX_EP_COUNT = 16;
#endif
        std::array<std::array<std::atomic_flag, MAX_EP_COUNT - 1>, 2> ep_flags {};

        void* kusb_handle() const { return kusb_handle_; }

        std::atomic_flag& ep_flag(usb::endpoint::address addr)
        {
            assert((addr.number() != 0) and (addr.number() <= MAX_EP_COUNT));
            return ep_flags[static_cast<size_t>(addr.direction())][addr.number() - 1];
        }

        static const controller_interface* kusb_index_to_if(usb_controller_index_t kusb_id);

        IRQn_Type kusb_irqn() const;

        void process_kusb_ep_notification(const usb_device_callback_message_struct_t& message);

        template <typename T = void>
        usb_status_t device_control(usb_device_control_type_t type, T* param = nullptr) const
        {
            return kusb_if_.deviceControl(kusb_handle(), type, static_cast<void*>(param));
        }
        template <typename T = uint8_t>
        usb_status_t device_send(uint8_t addr, T* data, uint32_t size) const
        {
            return kusb_if_.deviceSend(kusb_handle(), addr, const_cast<uint8_t*>(data), size);
        }
        usb_status_t device_recv(uint8_t addr, uint8_t* data, uint32_t size) const
        {
            return kusb_if_.deviceRecv(kusb_handle(), addr, data, size);
        }
        void set_address_early();
        void set_address_timely();

        void control_ep_open() override;
        void control_reply(usb::direction dir, const usb::df::transfer& t) override;

        void init(const usb::speeds& speeds) override;
        void deinit() override;
        void soft_attach() override;
        void soft_detach() override;
        void signal_remote_wakeup() override;

        usb::speed speed() const override;

        usb::df::ep_handle ep_open(const usb::df::config::endpoint& ep) override;
        usb::result ep_send(usb::df::ep_handle eph, const std::span<const uint8_t>& data) override;
        usb::result ep_receive(usb::df::ep_handle eph, const std::span<uint8_t>& data) override;
        usb::result ep_close(usb::df::ep_handle eph) override;
        usb::result ep_cancel(usb::df::ep_handle eph);

        bool ep_is_stalled(usb::df::ep_handle eph) const override;
        usb::result ep_change_stall(usb::df::ep_handle eph, bool stall) override;
    };
}

#endif // C2USB_HAS_KSDK_HEADERS

#endif // __USB_DF_PORT_NXP_USB_MAC_HPP_
