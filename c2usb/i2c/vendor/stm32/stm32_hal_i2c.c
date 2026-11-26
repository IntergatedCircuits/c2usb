// SPDX-License-Identifier: MPL-2.0
/*
 * Builds the STM32Cube HAL I2C driver of the SoC series as part of c2usb,
 * for the cases where the hal_stm32 module's build isn't suitable.
 *
 * I2C v1 IP series (F1, F2, F4, L1):
 * hal_stm32 disables the HAL I2C module of these series in their hal_conf.h, as its
 * I2C_SPEED_STANDARD and I2C_SPEED_FAST macros collide with <zephyr/drivers/i2c.h>.
 * HAL_I2C_MODULE_ENABLED is defined here, so the driver gets compiled.
 *
 * I2C v2 IP target only build (C2USB_STM32_HAL_I2C_TARGET_ONLY):
 * The HAL's I2C_Enable_IRQ() compares the transfer ISR against the addresses of the
 * controller (master) and memory mode DMA ISRs. Taking their addresses forces the linker
 * to keep them, along with all the controller mode code reachable from them, even though
 * no controller mode API is used.
 * Here the address references are substituted with a placeholder function, while the actual
 * ISR definitions are renamed (calls and definitions are function-like macro expansions,
 * address references aren't), turning them into unreferenced static functions,
 * which the compiler discards.
 * The controller mode DMA APIs of this HAL instance are not functional.
 */
#ifndef HAL_I2C_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
#endif
#include "st/stm32hal.h"

#if defined(C2USB_STM32_HAL_I2C_TARGET_ONLY)

static HAL_StatusTypeDef c2usb_i2c_removed_isr(struct __I2C_HandleTypeDef *hi2c, uint32_t ITFlags,
					       uint32_t ITSources)
{
	(void)hi2c;
	(void)ITFlags;
	(void)ITSources;
	return HAL_ERROR;
}

typedef HAL_StatusTypeDef (*c2usb_i2c_xfer_isr_t)(struct __I2C_HandleTypeDef *hi2c,
						 uint32_t ITFlags, uint32_t ITSources);

static const c2usb_i2c_xfer_isr_t I2C_Master_ISR_DMA = c2usb_i2c_removed_isr;
static const c2usb_i2c_xfer_isr_t I2C_Mem_ISR_DMA = c2usb_i2c_removed_isr;

#define I2C_Master_ISR_DMA(...) c2usb_i2c_unused_Master_ISR_DMA(__VA_ARGS__)
#define I2C_Mem_ISR_DMA(...)    c2usb_i2c_unused_Mem_ISR_DMA(__VA_ARGS__)

#pragma GCC diagnostic ignored "-Wunused-function"

#endif /* C2USB_STM32_HAL_I2C_TARGET_ONLY */

#include C2USB_STM32_HAL_I2C_SOURCE
