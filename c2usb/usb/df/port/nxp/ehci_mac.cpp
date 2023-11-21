#include "usb/df/port/nxp/kusb_mac.hpp"
#if C2USB_HAS_NXP_EHCI
#include "usb_device_ehci.h"

using namespace usb::df::nxp;

const kusb_mac::controller_interface ehci_mac::interface = {
    USB_DeviceEhciInit,   USB_DeviceEhciDeinit,  USB_DeviceEhciSend,       USB_DeviceEhciRecv,
    USB_DeviceEhciCancel, USB_DeviceEhciControl, USB_DeviceEhciIsrFunction};

#endif
