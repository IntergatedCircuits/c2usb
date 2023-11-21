#include "usb/df/port/nxp/kusb_mac.hpp"
#if C2USB_HAS_NXP_LPCIP3511
#include "usb_device_lpcip3511.h"

using namespace usb::df::nxp;

const kusb_mac::controller_interface lpcip3511_mac::interface = {
    USB_DeviceLpc3511IpInit,       USB_DeviceLpc3511IpDeinit, USB_DeviceLpc3511IpSend,
    USB_DeviceLpc3511IpRecv,       USB_DeviceLpc3511IpCancel, USB_DeviceLpc3511IpControl,
    USB_DeviceLpcIp3511IsrFunction};

#endif
