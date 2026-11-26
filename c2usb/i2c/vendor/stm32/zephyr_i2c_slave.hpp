// SPDX-License-Identifier: MPL-2.0
#pragma once

// this header is intentionally free of STM32 HAL / CMSIS includes,
// as their macro definitions collide with C++ identifiers
#include "i2c/slave.hpp"
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

namespace i2c::stm32::zephyr
{
/// @brief  Get the I2C slave driver of an STM32 I2C node with the
///         "c2usb,stm32-i2c-v1-target" or "c2usb,stm32-i2c-v2-target" compatible.
/// @param  dev: the device, acquired with DEVICE_DT_GET(node_id)
/// @return pointer to the slave driver, or nullptr if the device isn't ready
i2c::slave* get_slave(const ::device* dev);
} // namespace i2c::stm32::zephyr

/// @brief  Get the c2usb I2C slave driver of the I2C HID devicetree node's parent bus.
///         The node must be on the "i2c-target" bus, check with DT_ON_BUS(node_id, i2c_target).
#define C2USB_I2C_SLAVE_DT_GET(hid_node_id)                                                        \
    i2c::stm32::zephyr::get_slave(DEVICE_DT_GET(DT_BUS(hid_node_id)))

/// @brief  Get the I2C address of the I2C HID devicetree node.
#define C2USB_I2C_HID_DT_ADDRESS(hid_node_id) i2c::address(DT_REG_ADDR(hid_node_id))

/// @brief  Get the HID descriptor register address of the I2C HID devicetree node.
#define C2USB_I2C_HID_DT_DESCRIPTOR_REG(hid_node_id) DT_PROP(hid_node_id, hid_descr_addr)
