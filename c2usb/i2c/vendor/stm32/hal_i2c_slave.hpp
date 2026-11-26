// SPDX-License-Identifier: MPL-2.0
#pragma once

#include "i2c/slave.hpp"

// Zephyr's hal_stm32 leaves the HAL I2C module disabled in the hal_conf.h of the I2C v1 IP series,
// as its I2C_SPEED_* macros collide with <zephyr/drivers/i2c.h> (don't include both).
// Enabling it before the HAL is included makes hal_conf.h include the HAL I2C header.
#ifndef HAL_I2C_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
#endif
// includes the STM32Cube HAL of the target series, https://github.com/IntergatedCircuits/stm32header
#include "st/stm32hal.h"
#if !defined(I2C_ADDRESSINGMODE_7BIT)
#error "STM32 HAL I2C header missing: the HAL was included earlier without the I2C module enabled"
#endif

#if defined(I2C_TIMINGR_PRESC)
#define C2USB_STM32_I2C_V2 1 // I2C v2 IP, with TIMINGR register
#elif defined(I2C_CCR_CCR)
#define C2USB_STM32_I2C_V2 0 // I2C v1 IP, with CCR register
#else
#error "Unsupported STM32 I2C IP"
#endif

namespace i2c::stm32
{
/// @brief  I2C slave driver for the STM32 I2C v1 and v2 IPs,
///         using the STM32Cube HAL I2C and DMA drivers.
///         This class is platform agnostic, it operates on a HAL I2C handle
///         that is provided by the platform integration, which is responsible for
///         - configuring the I2C handle's Instance and Init fields (the own address
///           is set by this driver), and linking the DMA handles to it (hdmatx, hdmarx)
///         - enabling the I2C and DMA peripheral clocks, configuring the pins
///         - routing the I2C and DMA interrupts to the HAL IRQ handlers
///           (or the *_irq_handler() functions), at identical priority
///           (the HAL callbacks are invoked from both ISRs)
///         - implementing the INT pin control (set_pin_interrupt())
///         STM32CubeMX generated code (MX_I2Cx_Init(), HAL_I2C_MspInit() and the IRQ handlers)
///         fulfills all but the last requirement.
///         The HAL I2C callbacks (HAL_I2C_AddrCallback() etc.) are implemented
///         by this module, and dispatched to the instance that operates the handle.
class hal_i2c_slave : public i2c::slave
{
  public:
    /// @brief  Event IRQ handler, also handles errors on combined I2C interrupt lines (v2 only).
    void ev_irq_handler();
    /// @brief  Error IRQ handler, only needed for separate I2C error interrupt lines.
    void er_irq_handler();
    void tx_dma_irq_handler() { HAL_DMA_IRQHandler(hi2c_->hdmatx); }
    void rx_dma_irq_handler() { HAL_DMA_IRQHandler(hi2c_->hdmarx); }

    // HAL callback dispatch targets, not to be called by the application
    void handle_start(i2c::direction dir);
    void handle_tx_complete();
    void handle_rx_complete();
    void handle_stop();
    void handle_error();

    /// @brief  Finds the driver instance that operates the HAL I2C handle.
    /// @return the driver instance, or nullptr if the handle isn't operated by this driver
    static hal_i2c_slave* from_handle(const I2C_HandleTypeDef* hi2c);

    [[nodiscard]] I2C_HandleTypeDef& handle() const { return *hi2c_; }

    hal_i2c_slave(const hal_i2c_slave&) = delete;
    hal_i2c_slave& operator=(const hal_i2c_slave&) = delete;
    hal_i2c_slave(hal_i2c_slave&&) = delete;
    hal_i2c_slave& operator=(hal_i2c_slave&&) = delete;

  protected:
    /// @brief  Constructs the driver operating the given HAL I2C handle.
    /// @param  hi2c: the HAL I2C handle, see the class description for its requirements.
    ///         HAL_I2C_Init() is (re)invoked by this driver when listening is started.
    explicit hal_i2c_slave(I2C_HandleTypeDef& hi2c);
    ~hal_i2c_slave() override;

  private:
    void nack();
    void send_dummy();
    void send(const std::span<const uint8_t>& a) override;
    void send(const std::span<const uint8_t>& a, const std::span<const uint8_t>& b) override;
    void receive(const std::span<uint8_t>& a) override;
    void receive(const std::span<uint8_t>& a, const std::span<uint8_t>& b) override;
    void set_slave_address(i2c::address slave_addr);
    void start_listen(i2c::address slave_addr) override;
    void start_listen();
    void stop_listen(i2c::address slave_addr) override;
    size_t transferred_size(i2c::direction dir) const;

    // the instances are linked into a list, so the HAL callbacks can find them by handle
    // NOLINTNEXTLINE(*-avoid-non-const-global-variables)
    static inline hal_i2c_slave* instances_{};
    hal_i2c_slave* next_{};

    I2C_HandleTypeDef* hi2c_;
    size_t first_size_{};
    size_t second_size_{};
    uint8_t* second_data_{};
    i2c::direction last_dir_{};
};

/// @brief  HAL handles for platform integrations without generated HAL configuration code,
///         to be used as a base class preceding hal_i2c_slave (so it's constructed first).
///         The pins, clocks and interrupts remain the responsibility of the platform.
struct hal_handles
{
    I2C_HandleTypeDef i2c{};
    DMA_HandleTypeDef dma_tx{};
    DMA_HandleTypeDef dma_rx{};

    /// @brief  Configures the I2C handle, and links the DMA handles to it.
    /// @param  instance: the I2C peripheral
    /// @param  timing: v2 IP: the TIMINGR register value
    ///                 (even as target, the SDADEL and SCLDEL fields are in effect)
    ///                 v1 IP: the bus clock frequency in Hz
    ///                 (the HAL derives the configuration from it and the APB1 clock)
    hal_handles(I2C_TypeDef* instance, uint32_t timing);
    ~hal_handles();

    /// @brief  Initializes the HAL DMA handles.
    ///         The DMA handles' Instance and Init fields must be set before calling this.
    /// @return true if successful
    bool init_dma();

    hal_handles(const hal_handles&) = delete;
    hal_handles& operator=(const hal_handles&) = delete;
    hal_handles(hal_handles&&) = delete;
    hal_handles& operator=(hal_handles&&) = delete;
};
} // namespace i2c::stm32
