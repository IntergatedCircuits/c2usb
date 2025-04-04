#include "port/nxp/mcux_mac.hpp"
#if C2USB_HAS_NXP_LPCIP3511
#include "port/nxp/controller_interface.hpp"
#include <usb_device_lpcip3511.h>

using namespace usb::df::nxp;

namespace usb::df::nxp
{
static const controller_interface lpcip3511_mac_interface = {
    USB_DeviceLpc3511IpInit,       USB_DeviceLpc3511IpDeinit, USB_DeviceLpc3511IpSend,
    USB_DeviceLpc3511IpRecv,       USB_DeviceLpc3511IpCancel, USB_DeviceLpc3511IpControl,
    USB_DeviceLpcIp3511IsrFunction};

lpcip3511_mac::lpcip3511_mac(usb::speed speed)
    : mcux_mac(speed == usb::speed::HIGH ? kUSB_ControllerLpcIp3511Hs0
                                         : kUSB_ControllerLpcIp3511Fs0,
               lpcip3511_mac_interface)
{}

} // namespace usb::df::nxp
#endif // C2USB_HAS_NXP_LPCIP3511
