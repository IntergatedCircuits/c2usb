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
#include "port/nxp/mcux_mac.hpp"
#include "usb/standard/requests.hpp"
#include <usb_device.h>
#include <usb_device_config.h>
#include <usb_device_dci.h>
#if C2USB_HAS_NXP_DWC3
#include <usb_device_dwc3.h>
#endif
#if C2USB_HAS_NXP_EHCI
#include <usb_device_ehci.h>
#endif
#if C2USB_HAS_NXP_KHCI
#include <fsl_device_registers.h>
#include <usb_device_khci.h>
#endif
#if C2USB_HAS_NXP_LPCIP3511
#include <usb_device_lpcip3511.h>
#endif

namespace usb::df::nxp
{
struct controller_interface : public ::_usb_device_controller_interface_struct
{
    void (*isr)(void* param);

    template <typename T = void>
    usb_status_t device_control(void* handle, usb_device_control_type_t type,
                                T* param = nullptr) const
    {
        return deviceControl(handle, type, static_cast<void*>(param));
    }
};

#if C2USB_HAS_NXP_DWC3
mcux_mac mcux_mac::dwc3(const std::span<uint8_t>& control_buffer)
{
    static const controller_interface dwc3_mac_interface = {
        USB_DeviceDwc3Init,   USB_DeviceDwc3Deinit,  USB_DeviceDwc3Send,       USB_DeviceDwc3Recv,
        USB_DeviceDwc3Cancel, USB_DeviceDwc3Control, USB_DeviceDwc3IsrFunction};
    return mcux_mac(kUSB_ControllerDwc30, dwc3_mac_interface, control_buffer);
}
#endif // C2USB_HAS_NXP_DWC3
#if C2USB_HAS_NXP_EHCI

mcux_mac mcux_mac::ehci(const std::span<uint8_t>& control_buffer)
{
    static const controller_interface ehci_mac_interface = {
        USB_DeviceEhciInit,   USB_DeviceEhciDeinit,  USB_DeviceEhciSend,       USB_DeviceEhciRecv,
        USB_DeviceEhciCancel, USB_DeviceEhciControl, USB_DeviceEhciIsrFunction};
    return mcux_mac(kUSB_ControllerEhci0, ehci_mac_interface, control_buffer);
}
#endif // C2USB_HAS_NXP_EHCI
#if C2USB_HAS_NXP_KHCI
mcux_mac mcux_mac::khci(const std::span<uint8_t>& control_buffer)
{
    static const controller_interface khci_mac_interface = {
        USB_DeviceKhciInit,   USB_DeviceKhciDeinit,  USB_DeviceKhciSend,       USB_DeviceKhciRecv,
        USB_DeviceKhciCancel, USB_DeviceKhciControl, USB_DeviceKhciIsrFunction};

    // allow USB controller to read from Flash
#ifdef FMC_PFAPR_M3AP_SHIFT
    FMC->PFAPR |= (1 << FMC_PFAPR_M3AP_SHIFT);
#endif
#ifdef FMC_PFAPR_M4AP_SHIFT
    FMC->PFAPR |= (1 << FMC_PFAPR_M4AP_SHIFT);
#endif
    return mcux_mac(kUSB_ControllerKhci0, khci_mac_interface, control_buffer);
}
#endif // C2USB_HAS_NXP_KHCI
#if C2USB_HAS_NXP_LPCIP3511
mcux_mac mcux_mac::lpcip3511(const std::span<uint8_t>& control_buffer, usb::speed speed)
{
    static const controller_interface lpcip3511_mac_interface = {
        USB_DeviceLpc3511IpInit,       USB_DeviceLpc3511IpDeinit, USB_DeviceLpc3511IpSend,
        USB_DeviceLpc3511IpRecv,       USB_DeviceLpc3511IpCancel, USB_DeviceLpc3511IpControl,
        USB_DeviceLpcIp3511IsrFunction};
    return mcux_mac(speed == usb::speed::HIGH ? kUSB_ControllerLpcIp3511Hs0
                                              : kUSB_ControllerLpcIp3511Fs0,
                    lpcip3511_mac_interface, control_buffer);
}
#endif // C2USB_HAS_NXP_LPCIP3511

static IRQn_Type usb_irqn(int index)
{
    static constexpr IRQn_Type usb_irqs[] = USB_IRQS;
    return usb_irqs[index & 1];
}

void mcux_mac::init(const speeds& speeds)
{
    [[maybe_unused]] auto status = driver_.deviceInit(index_, this, &handle_);
    assert(status == kStatus_USB_Success);

    EnableIRQ(usb_irqn(index_));
}

void mcux_mac::deinit()
{
    stop();
    DisableIRQ(usb_irqn(index_));

    [[maybe_unused]] auto status = driver_.deviceDeinit(handle());
    assert(status == kStatus_USB_Success);
}

bool mcux_mac::set_attached(bool attached)
{
    driver_.device_control(handle(), attached ? kUSB_DeviceControlRun : kUSB_DeviceControlStop);
    return attached;
}

usb::result mcux_mac::signal_remote_wakeup()
{
    auto status = driver_.device_control(handle(), kUSB_DeviceControlResume);
    switch (status)
    {
    case kStatus_USB_Success:
        return usb::result::ok;
    default:
        return usb::result::io_error;
    }
}

void mcux_mac::set_address_early()
{
    driver_.device_control(handle(), kUSB_DeviceControlPreSetDeviceAddress,
                           &request().wValue.low_byte());
}

void mcux_mac::set_address_timely()
{
    driver_.device_control(handle(), kUSB_DeviceControlSetDeviceAddress,
                           &request().wValue.low_byte());
}

usb::speed mcux_mac::speed() const
{
    uint8_t sp;
    driver_.device_control(handle(), kUSB_DeviceControlGetSpeed, &sp);
    switch (sp)
    {
    case USB_SPEED_LOW:
        return usb::speed::LOW;
    case USB_SPEED_HIGH:
        return usb::speed::HIGH;
        // case USB_SPEED_SUPER:
    case USB_SPEED_FULL:
    default:
        return usb::speed::FULL;
    }
}

void mcux_mac::control_ep_open()
{
    auto mps = control_ep_max_packet_size(speed());
    ep_init(endpoint::address::control_out(), usb::endpoint::type::CONTROL, mps, 0);
    ep_init(endpoint::address::control_in(), usb::endpoint::type::CONTROL, mps, 0);
}

void mcux_mac::control_ep_stall()
{
    auto addr = endpoint::address::control_out();
    driver_.device_control(handle(), kUSB_DeviceControlEndpointStall, &addr);
}

bool mcux_mac::ep_init(usb::endpoint::address addr, usb::endpoint::type type, uint16_t mps,
                       uint8_t interval)
{
    usb_device_endpoint_init_struct_t ep_init{};
    ep_init.zlt = type == usb::endpoint::type::CONTROL;
    ep_init.transferType = static_cast<uint8_t>(type);
    ep_init.interval = interval;
    ep_init.endpointAddress = addr;
    ep_init.maxPacketSize = mps;
    return driver_.device_control(handle(), kUSB_DeviceControlEndpointInit, &ep_init) ==
           kStatus_USB_Success;
}

usb::df::ep_handle mcux_mac::ep_open(const config::endpoint& ep)
{
    return create_ep_handle(
        ep_init(ep.address(), ep.type(), ep.max_packet_size(), ep.interval()) ? ep.address() : 0);
}

usb::result mcux_mac::ep_send(ep_handle eph, const std::span<const uint8_t>& data)
{
    auto addr = ep_handle_to_address(eph);
    if (power_state() != power::state::L0_ON)
    {
        return result::network_down;
    }
    if (busy_flags_.test_and_set(addr))
    {
        return usb::result::device_or_resource_busy;
    }
    auto status =
        driver_.deviceSend(handle(), addr, const_cast<uint8_t*>(data.data()), data.size());
    return (status == kStatus_USB_Success) ? usb::result::ok : usb::result::not_connected;
}

usb::result mcux_mac::ep_receive(ep_handle eph, const std::span<uint8_t>& data)
{
    auto addr = ep_handle_to_address(eph);
    if (busy_flags_.test_and_set(addr))
    {
        return usb::result::device_or_resource_busy;
    }
    auto status = driver_.deviceRecv(handle(), addr, data.data(), data.size());
    return (status == kStatus_USB_Success) ? usb::result::ok : usb::result::not_connected;
}

usb::result mcux_mac::ep_cancel(ep_handle eph)
{
    auto addr = ep_handle_to_address(eph);
    auto status = driver_.deviceCancel(handle(), addr);
    busy_flags_.clear(addr);
    return (status == kStatus_USB_Success) ? usb::result::ok : usb::result::not_connected;
}

usb::result mcux_mac::ep_close(ep_handle eph)
{
    auto addr = ep_handle_to_address(eph);
    auto status = driver_.device_control(handle(), kUSB_DeviceControlEndpointDeinit, &addr);
    busy_flags_.clear(addr);
    return (status == kStatus_USB_Success) ? usb::result::ok : usb::result::not_connected;
}

bool mcux_mac::ep_is_stalled(ep_handle eph) const
{
    usb_device_endpoint_status_struct_t ep_status{};
    ep_status.endpointAddress = ep_handle_to_address(eph);
    [[maybe_unused]] auto status =
        driver_.device_control(handle(), kUSB_DeviceControlGetEndpointStatus, &ep_status);
    assert(status == kStatus_USB_Success);
    return ep_status.endpointStatus == kUSB_DeviceEndpointStateStalled;
}

usb::result mcux_mac::ep_change_stall(ep_handle eph, bool stall)
{
    auto addr = ep_handle_to_address(eph);
    auto status = driver_.device_control(
        handle(), stall ? kUSB_DeviceControlEndpointStall : kUSB_DeviceControlEndpointUnstall,
        &addr);
    return (status == kStatus_USB_Success) ? usb::result::ok : usb::result::not_connected;
}

void mcux_mac::process_ep_notification(const _usb_device_callback_message_struct& message)
{
    endpoint::address addr{static_cast<uint8_t>(message.code)};
    if (not addr.valid())
    {
        // wrong mapping
    }
    else if (message.length == UINT32_MAX)
    {
        // callback due to ep_cancel()
    }
    else if (addr.number() == 0)
    {
        transfer t{};
        direction dir{};
        if (message.isSetup)
        {
            if ((message.buffer == nullptr) or (message.length != sizeof(request())))
            {
                return control_ep_stall();
            }
            std::memcpy(&request(), message.buffer, sizeof(request()));
            t = control_ep_setup();
            if (t.stalled())
            {
                return control_ep_stall();
            }
            if (request() == standard::device::SET_ADDRESS)
            {
                set_address_early();
            }
            dir = (request().wLength == 0) ? direction::IN : request().direction();
        }
        else if (message.length > 0)
        {
            if (!control_ep_data(addr.direction(), transfer(message.buffer, message.length)))
            {
                return control_ep_stall();
            }
            dir = opposite_direction(addr.direction());
        }
        else
        {
            // control_ep_status(addr.direction());
            if (request() == standard::device::SET_ADDRESS)
            {
                set_address_timely();
            }
            return;
        }

        // prepare CTRL EP for next transfer
        if (dir == direction::IN)
        {
            [[maybe_unused]] auto status =
                driver_.deviceSend(handle(), endpoint::address::control_in(), t.data(), t.size());
            assert(status == kStatus_USB_Success);
        }
        else
        {
            [[maybe_unused]] auto status =
                driver_.deviceRecv(handle(), endpoint::address::control_out(), t.data(), t.size());
            assert((status == kStatus_USB_Success) or
                   (t.size() == 0)); // work around bug in NXP code
        }
    }
    else
    {
        busy_flags_.clear(addr);
        ep_transfer_complete(addr, create_ep_handle(addr),
                             transfer(message.buffer, message.length));
    }
}

void mcux_mac::handle_irq()
{
    void* device_handle =
        reinterpret_cast<char*>(&handle_) - offsetof(usb_device_struct_t, controllerHandle);
    driver_.isr(device_handle);
}

void mcux_mac::process_notification(const _usb_device_callback_message_struct& message)
{
    switch (message.code)
    {
    case kUSB_DeviceNotifyBusReset:
        driver_.device_control(handle(), kUSB_DeviceControlSetDefaultStatus);
        bus_reset();
        control_ep_open();
        break;
    case kUSB_DeviceNotifySuspend:
        set_power_state(power::state::L2_SUSPEND);
        break;
    case kUSB_DeviceNotifyResume:
        set_power_state(power::state::L0_ON);
        break;
    case kUSB_DeviceNotifyLPMSleep:
        set_power_state(power::state::L1_SLEEP);
        break;

    case kUSB_DeviceNotifyDetach:
        set_power_state(power::state::L3_OFF);
        break;
    case kUSB_DeviceNotifyAttach:
        set_power_state(power::state::L2_SUSPEND);
        break;
        // case kUSB_DeviceNotifyDcdDetectFinished:
    case kUSB_DeviceNotifyError:
        break;
    default:
        process_ep_notification(message);
        break;
    }
}

extern "C" usb_status_t USB_DeviceNotificationTrigger(void* handle, void* msg)
{
    assert(msg != nullptr);
    if (handle == nullptr)
    {
        return kStatus_USB_InvalidHandle;
    }
    else
    {
        auto* mac = reinterpret_cast<mcux_mac*>(handle);
        mac->process_notification(
            *reinterpret_cast<const _usb_device_callback_message_struct*>(msg));
        return kStatus_USB_Success;
    }
}

} // namespace usb::df::nxp
