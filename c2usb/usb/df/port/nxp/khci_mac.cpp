#include "usb/df/port/nxp/kusb_mac.hpp"
#if C2USB_HAS_NXP_KHCI
#include "usb_device_khci.h"

using namespace usb::df::nxp;

const kusb_mac::controller_interface khci_mac::interface = {
    USB_DeviceKhciInit,   USB_DeviceKhciDeinit,  USB_DeviceKhciSend,       USB_DeviceKhciRecv,
    USB_DeviceKhciCancel, USB_DeviceKhciControl, USB_DeviceKhciIsrFunction};

#endif
