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
#ifndef __USB_DF_PORT_NXP_CONTROLLER_INTERFACE_HPP_
#define __USB_DF_PORT_NXP_CONTROLLER_INTERFACE_HPP_
#include <usb_device_config.h>

#include <usb_device.h>

#include <usb_device_dci.h>

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
} // namespace usb::df::nxp

#endif // __USB_DF_PORT_NXP_CONTROLLER_INTERFACE_HPP_
