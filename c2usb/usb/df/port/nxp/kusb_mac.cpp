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
#include "usb/df/port/nxp/kusb_mac.hpp"
#include "usb/standard/requests.hpp"
#include <chrono>

#if C2USB_HAS_NXP_HEADERS

using namespace usb::df::nxp;
using namespace usb::df;
using namespace usb;

IRQn_Type kusb_mac::kusb_irqn() const
{
    static constexpr IRQn_Type kusb_irqs[] = USB_IRQS;
    return kusb_irqs[0];
}

void kusb_mac::init(const speeds& speeds)
{
    auto status = kusb_if_.deviceInit(kusb_id_, this, &kusb_handle_);
    assert(status == kStatus_USB_Success);

    EnableIRQ(kusb_irqn());

    // allow USB controller to read from Flash
#ifdef FMC_PFAPR_M3AP_SHIFT
    FMC->PFAPR |= (1 << FMC_PFAPR_M3AP_SHIFT);
#endif
#ifdef FMC_PFAPR_M4AP_SHIFT
    FMC->PFAPR |= (1 << FMC_PFAPR_M4AP_SHIFT);
#endif
}

void kusb_mac::deinit()
{
    stop();
    DisableIRQ(kusb_irqn());

    auto status = kusb_if_.deviceDeinit(kusb_handle_);
    assert(status == kStatus_USB_Success);
}

void kusb_mac::soft_attach()
{
    device_control(kUSB_DeviceControlRun);
}

void kusb_mac::soft_detach()
{
    device_control(kUSB_DeviceControlStop);
}

void kusb_mac::signal_remote_wakeup()
{
    device_control(kUSB_DeviceControlResume);
}

void kusb_mac::set_address_early()
{
    device_control(kUSB_DeviceControlPreSetDeviceAddress, &request().wValue.low_byte());
}

void kusb_mac::set_address_timely()
{
    device_control(kUSB_DeviceControlSetDeviceAddress, &request().wValue.low_byte());
}

usb::speed kusb_mac::speed() const
{
    uint8_t sp;
    device_control(kUSB_DeviceControlGetSpeed, &sp);
    switch (sp)
    {
        case USB_SPEED_LOW:
            return usb::speed::LOW;
        case USB_SPEED_HIGH:
            return usb::speed::HIGH;
            //case USB_SPEED_SUPER:
        case USB_SPEED_FULL:
        default:
            return usb::speed::FULL;
    }
}

void kusb_mac::control_ep_open()
{
    usb_device_endpoint_init_struct_t ep_init {};
    ep_init.zlt             = true;
    ep_init.transferType    = USB_ENDPOINT_CONTROL;
    ep_init.maxPacketSize   = control_ep_max_packet_size(speed());
    ep_init.endpointAddress = endpoint::address::control_out();
    auto status = device_control(kUSB_DeviceControlEndpointInit, &ep_init);
    assert(status == kStatus_USB_Success);

    ep_init.endpointAddress = endpoint::address::control_in();
    status = device_control(kUSB_DeviceControlEndpointInit, &ep_init);
    assert(status == kStatus_USB_Success);
}

void kusb_mac::control_reply(usb::direction dir, const usb::df::transfer& t)
{
    auto addr = endpoint::address::control(dir);
    if (t.stalled())
    {
        device_control(kUSB_DeviceControlEndpointStall, &addr);
    }
    else if (dir == direction::IN)
    {
        if (request() == standard::device::SET_ADDRESS)
        {
            set_address_early();
        }

        auto status = device_send(addr, t.data(), t.size());
        assert(status == kStatus_USB_Success);
    }
    else
    {
        auto status = device_recv(addr, t.data(), t.size());
        assert((status == kStatus_USB_Success)
                or (t.size() == 0)); // work around bug in NXP code
    }
}

usb::df::ep_handle kusb_mac::ep_open(const config::endpoint& ep)
{
    assert(ep.address().number() < MAX_EP_COUNT);
    usb_device_endpoint_init_struct_t ep_init {};
    ep_init.zlt = false;
    ep_init.transferType = static_cast<uint8_t>(ep.type());
    ep_init.interval = ep.interval();
    ep_init.endpointAddress = ep.address();
    ep_init.maxPacketSize = USB_CONTROL_MAX_PACKET_SIZE;
    auto status = device_control(kUSB_DeviceControlEndpointInit, &ep_init);
    return create_ep_handle((status == kStatus_USB_Success) ? ep.address() : 0);
}

usb::result kusb_mac::ep_send(ep_handle eph, const std::span<const uint8_t>& data)
{
    auto addr = ep_handle_to_address(eph);
    if (ep_flag(addr).test_and_set())
    {
        return usb::result::BUSY;
    }
    auto status = device_send(addr, data.data(), data.size());
    return (status == kStatus_USB_Success) ? usb::result::OK : usb::result::NO_CONNECTION;
}

usb::result kusb_mac::ep_receive(ep_handle eph, const std::span<uint8_t>& data)
{
    auto addr = ep_handle_to_address(eph);
    if (ep_flag(addr).test_and_set())
    {
        return usb::result::BUSY;
    }
    auto status = device_recv(addr, data.data(), data.size());
    return (status == kStatus_USB_Success) ? usb::result::OK : usb::result::NO_CONNECTION;
}

usb::result kusb_mac::ep_cancel(ep_handle eph)
{
    auto addr = ep_handle_to_address(eph);
    auto status = kusb_if_.deviceCancel(kusb_handle(), addr);
    ep_flag(addr).clear();
    return (status == kStatus_USB_Success) ? usb::result::OK : usb::result::NO_CONNECTION;
}

usb::result kusb_mac::ep_close(ep_handle eph)
{
    auto addr = ep_handle_to_address(eph);
    auto status = device_control(kUSB_DeviceControlEndpointDeinit, &addr);
    ep_flag(addr).clear();
    return (status == kStatus_USB_Success) ? usb::result::OK : usb::result::NO_CONNECTION;
}

bool kusb_mac::ep_is_stalled(ep_handle eph) const
{
    usb_device_endpoint_status_struct_t ep_status {};
    ep_status.endpointAddress = ep_handle_to_address(eph);
    auto status = device_control(kUSB_DeviceControlGetEndpointStatus, &ep_status);
    assert(status == kStatus_USB_Success);
    return ep_status.endpointStatus == kUSB_DeviceEndpointStateStalled;
}

usb::result kusb_mac::ep_change_stall(ep_handle eph, bool stall)
{
    usb_status_t status;
    auto addr = ep_handle_to_address(eph);
    if (stall)
    {
        status = device_control(kUSB_DeviceControlEndpointStall, &addr);
    }
    else
    {
        status = device_control(kUSB_DeviceControlEndpointUnstall, &addr);
    }
    return (status == kStatus_USB_Success) ? usb::result::OK : usb::result::NO_CONNECTION;
}

void kusb_mac::process_kusb_ep_notification(const usb_device_callback_message_struct_t& message)
{
    endpoint::address addr { static_cast<uint8_t>(message.code) };
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
        if (message.isSetup
            and (stage() != usb::control::stage::DATA)) // work around bug in NXP code
        {
            assert((message.buffer != nullptr) and (message.length == sizeof(request())));
            std::memcpy(&request(), message.buffer, sizeof(request()));
            control_ep_setup();
        }
        else
        {
            control_ep_data(addr.direction(), transfer(message.buffer, message.length));

            if (request() == standard::device::SET_ADDRESS)
            {
                set_address_timely();
            }
        }
    }
    else
    {
        ep_flag(addr).clear();
        ep_transfer_complete(create_ep_handle(addr), transfer(message.buffer, message.length));
    }
}

void kusb_mac::handle_irq()
{
    void* device_handle = reinterpret_cast<char*>(&kusb_handle_) - offsetof(usb_device_struct_t, controllerHandle);
    kusb_if_.isr(device_handle);
}

void kusb_mac::process_kusb_notification(const usb_device_callback_message_struct_t& message)
{
    switch (message.code)
    {
        case kUSB_DeviceNotifyBusReset:
            device_control(kUSB_DeviceControlSetDefaultStatus);
            bus_reset();
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
        case kUSB_DeviceNotifyAttach:
            //case kUSB_DeviceNotifyDcdDetectFinished:
        case kUSB_DeviceNotifyError:
            break;
        default:
            process_kusb_ep_notification(message);
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
        auto* mac = reinterpret_cast<kusb_mac*>(handle);
        mac->process_kusb_notification(*reinterpret_cast<const usb_device_callback_message_struct_t*>(msg));
        return kStatus_USB_Success;
    }
}

#endif // C2USB_HAS_NXP_HEADERS
