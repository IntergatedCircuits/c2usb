#include "port/nxp/mcux_mac.hpp"
#if C2USB_HAS_NXP_KHCI
#include "port/nxp/controller_interface.hpp"
#include <fsl_device_registers.h>
#include <usb_device_khci.h>

namespace usb::df::nxp
{
static const controller_interface khci_mac_interface = {
    USB_DeviceKhciInit,   USB_DeviceKhciDeinit,  USB_DeviceKhciSend,       USB_DeviceKhciRecv,
    USB_DeviceKhciCancel, USB_DeviceKhciControl, USB_DeviceKhciIsrFunction};

mcux_mac mcux_mac::khci()
{
    // allow USB controller to read from Flash
#ifdef FMC_PFAPR_M3AP_SHIFT
    FMC->PFAPR |= (1 << FMC_PFAPR_M3AP_SHIFT);
#endif
#ifdef FMC_PFAPR_M4AP_SHIFT
    FMC->PFAPR |= (1 << FMC_PFAPR_M4AP_SHIFT);
#endif
    return mcux_mac(kUSB_ControllerKhci0, khci_mac_interface);
}

} // namespace usb::df::nxp
#endif // C2USB_HAS_NXP_KHCI
