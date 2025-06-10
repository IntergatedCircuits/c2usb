#include "port/nxp/mcux_mac.hpp"
#if C2USB_HAS_NXP_DWC3
#include "port/nxp/controller_interface.hpp"
#include <usb_device_dwc3.h>

namespace usb::df::nxp
{
static const controller_interface dwc3_mac_interface = {
    USB_DeviceDwc3Init,   USB_DeviceDwc3Deinit,  USB_DeviceDwc3Send,       USB_DeviceDwc3Recv,
    USB_DeviceDwc3Cancel, USB_DeviceDwc3Control, USB_DeviceDwc3IsrFunction};

mcux_mac mcux_mac::dwc3()
{
    return mcux_mac(kUSB_ControllerDwc30, dwc3_mac_interface);
}

} // namespace usb::df::nxp
#endif // C2USB_HAS_NXP_DWC3
