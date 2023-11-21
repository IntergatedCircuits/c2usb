#include "usb/df/port/nxp/kusb_mac.hpp"
#if C2USB_HAS_NXP_DWC3
#include "usb_device_dwc3.h"

using namespace usb::df::nxp;

const kusb_mac::controller_interface dwc3_mac::interface = {
    USB_DeviceDwc3Init,   USB_DeviceDwc3Deinit,  USB_DeviceDwc3Send,       USB_DeviceDwc3Recv,
    USB_DeviceDwc3Cancel, USB_DeviceDwc3Control, USB_DeviceDwc3IsrFunction};

#endif
