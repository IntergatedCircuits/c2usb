#include "port/nxp/mcux_mac.hpp"
#if C2USB_HAS_NXP_EHCI
#include "port/nxp/controller_interface.hpp"
#include <usb_device_ehci.h>

namespace usb::df::nxp
{
static const controller_interface ehci_mac_interface = {
    USB_DeviceEhciInit,   USB_DeviceEhciDeinit,  USB_DeviceEhciSend,       USB_DeviceEhciRecv,
    USB_DeviceEhciCancel, USB_DeviceEhciControl, USB_DeviceEhciIsrFunction};

mcux_mac mcux_mac::ehci()
{
    return mcux_mac(kUSB_ControllerEhci0, ehci_mac_interface);
}

} // namespace usb::df::nxp
#endif // C2USB_HAS_NXP_EHCI
