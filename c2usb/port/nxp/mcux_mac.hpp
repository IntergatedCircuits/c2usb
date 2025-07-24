/// @file
///
/// @author Benedek Kupper
/// @date   2025
///
/// @copyright
///         This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
///         If a copy of the MPL was not distributed with this file, You can obtain one at
///         https://mozilla.org/MPL/2.0/.
///
#ifndef __USB_DF_PORT_NXP_MCUX_MAC_HPP_
#define __USB_DF_PORT_NXP_MCUX_MAC_HPP_

#include <usb_device_config.h>
#include "usb/df/ep_flags.hpp"
#include "usb/df/mac.hpp"

#define C2USB_HAS_NXP_KHCI      (USB_DEVICE_CONFIG_KHCI)
#define C2USB_HAS_NXP_EHCI      (USB_DEVICE_CONFIG_EHCI)
#define C2USB_HAS_NXP_LPCIP3511 (USB_DEVICE_CONFIG_LPCIP3511FS or USB_DEVICE_CONFIG_LPCIP3511HS)
#define C2USB_HAS_NXP_DWC3      (0)

struct _usb_device_callback_message_struct;

namespace usb::df::nxp
{
struct controller_interface;

/// @brief  The mcux_mac implements the MAC interface to the NXP USB device stack.
class mcux_mac : public df::address_handle_mac
{
  public:
    void process_notification(const ::_usb_device_callback_message_struct& message);

    void handle_irq();

#if C2USB_HAS_NXP_KHCI
    static mcux_mac khci();
#endif // C2USB_HAS_NXP_KHCI

#if C2USB_HAS_NXP_EHCI
    static mcux_mac ehci();
#endif // C2USB_HAS_NXP_EHCI

#if C2USB_HAS_NXP_LPCIP3511
    static mcux_mac lpcip3511(usb::speed speed = usb::speed::FULL);
#endif // C2USB_HAS_NXP_LPCIP3511

#if C2USB_HAS_NXP_DWC3
    static mcux_mac dwc3();
#endif // C2USB_HAS_NXP_DWC3

  protected:
    constexpr mcux_mac(int usb_controller_index, const controller_interface& driver)
        : address_handle_mac(), driver_(driver), index_(usb_controller_index)
    {}

  private:
    void* handle_{};
    const controller_interface& driver_;
    int index_; // usb_controller_index_t
    ep_flags busy_flags_{};

    void* handle() const { return handle_; }

    void process_ep_notification(const ::_usb_device_callback_message_struct& message);

    void set_address_early();
    void set_address_timely();

    bool ep_init(usb::endpoint::address addr, usb::endpoint::type type, uint16_t mps,
                 uint8_t interval);
    void control_ep_open();
    void control_ep_stall();

    void init(const usb::speeds& speeds) override;
    void deinit() override;
    bool set_attached(bool attached) override;
    usb::result signal_remote_wakeup() override;

    usb::speed speed() const override;

    usb::df::ep_handle ep_open(const usb::df::config::endpoint& ep) override;
    usb::result ep_send(usb::df::ep_handle eph, const std::span<const uint8_t>& data) override;
    usb::result ep_receive(usb::df::ep_handle eph, const std::span<uint8_t>& data) override;
    usb::result ep_close(usb::df::ep_handle eph) override;
    usb::result ep_cancel(usb::df::ep_handle eph);

    bool ep_is_stalled(usb::df::ep_handle eph) const override;
    usb::result ep_change_stall(usb::df::ep_handle eph, bool stall) override;
};

} // namespace usb::df::nxp

#endif // __USB_DF_PORT_NXP_MCUX_MAC_HPP_
