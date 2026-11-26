// SPDX-License-Identifier: MPL-2.0
#include "i2c/vendor/stm32/hal_i2c_slave.hpp"
#include <array>
#include <cassert>

namespace i2c::stm32
{
hal_handles::hal_handles(I2C_TypeDef* instance, uint32_t timing)
{
    i2c.Instance = instance;
#if C2USB_STM32_I2C_V2
    i2c.Init.Timing = timing;
    i2c.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
#else
    i2c.Init.ClockSpeed = timing;
    i2c.Init.DutyCycle = I2C_DUTYCYCLE_2; // only relevant for controller mode
#endif
    i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    i2c.Init.OwnAddress2 = 0;
    i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    __HAL_LINKDMA(&i2c, hdmatx, dma_tx);
    __HAL_LINKDMA(&i2c, hdmarx, dma_rx);
}

hal_handles::~hal_handles()
{
    HAL_I2C_DeInit(&i2c);
    if (dma_tx.Instance != nullptr)
    {
        HAL_DMA_DeInit(&dma_tx);
    }
    if (dma_rx.Instance != nullptr)
    {
        HAL_DMA_DeInit(&dma_rx);
    }
}

bool hal_handles::init_dma()
{
    return (HAL_DMA_Init(&dma_tx) == HAL_OK) and (HAL_DMA_Init(&dma_rx) == HAL_OK);
}

hal_i2c_slave::hal_i2c_slave(I2C_HandleTypeDef& hi2c)
    : next_(instances_), hi2c_(&hi2c)
{
    assert(from_handle(&hi2c) == nullptr); // a handle can only be operated by one instance
    instances_ = this;
}

hal_i2c_slave::~hal_i2c_slave()
{
    HAL_I2C_DisableListen_IT(hi2c_);
    for (auto** link = &instances_; *link != nullptr; link = &(*link)->next_)
    {
        if (*link == this)
        {
            *link = next_;
            break;
        }
    }
}

hal_i2c_slave* hal_i2c_slave::from_handle(const I2C_HandleTypeDef* hi2c)
{
    auto* inst = instances_;
    while ((inst != nullptr) and (inst->hi2c_ != hi2c))
    {
        inst = inst->next_;
    }
    return inst;
}

void hal_i2c_slave::ev_irq_handler()
{
    auto* hi2c = hi2c_;
#if C2USB_STM32_I2C_V2
    // on combined interrupt lines the error flags must be checked here
    if ((hi2c->Instance->ISR & (I2C_FLAG_BERR | I2C_FLAG_ARLO | I2C_FLAG_OVR)) != 0)
    {
        HAL_I2C_ER_IRQHandler(hi2c);
        return;
    }
#endif
    HAL_I2C_EV_IRQHandler(hi2c);
}

void hal_i2c_slave::er_irq_handler()
{
    HAL_I2C_ER_IRQHandler(hi2c_);
}

void hal_i2c_slave::set_slave_address(i2c::address slave_addr)
{
    auto& init = hi2c_->Init;
    if (slave_addr.is_10bit())
    {
        // strip the 10-bit mode marker of the raw address
        init.OwnAddress1 = slave_addr.raw() & 0x3ffU;
        init.AddressingMode = I2C_ADDRESSINGMODE_10BIT;
    }
    else
    {
        init.OwnAddress1 = static_cast<uint32_t>(slave_addr.raw()) << 1;
        init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    }
    HAL_I2C_Init(hi2c_);
}

void hal_i2c_slave::start_listen(i2c::address slave_addr)
{
    set_slave_address(slave_addr);
    start_listen();
}

void hal_i2c_slave::start_listen()
{
    HAL_I2C_EnableListen_IT(hi2c_);
}

void hal_i2c_slave::stop_listen([[maybe_unused]] i2c::address slave_addr)
{
    HAL_I2C_DisableListen_IT(hi2c_);
}

void hal_i2c_slave::nack()
{
#if C2USB_STM32_I2C_V2
    __HAL_I2C_GENERATE_NACK(hi2c_);
#else
    // NACK the following received bytes, the HAL re-enables ACK when listening is restarted
    CLEAR_BIT(hi2c_->Instance->CR1, I2C_CR1_ACK);
#endif
}

void hal_i2c_slave::send_dummy()
{
    // the bus is released (idle high) bytes, until the master NACKs
    static constexpr std::array<uint8_t, 4> dummy{0xff, 0xff, 0xff, 0xff};
    HAL_I2C_Slave_Seq_Transmit_IT(hi2c_, const_cast<uint8_t*>(dummy.data()), dummy.size(),
                                  I2C_NEXT_FRAME);
}

void hal_i2c_slave::send(const std::span<const uint8_t>& a)
{
    first_size_ = a.size();
    second_size_ = 0;
    second_data_ = nullptr;
    HAL_I2C_Slave_Seq_Transmit_DMA(hi2c_, const_cast<uint8_t*>(a.data()), a.size(), I2C_NEXT_FRAME);
}

void hal_i2c_slave::send(const std::span<const uint8_t>& a, const std::span<const uint8_t>& b)
{
    first_size_ = a.size();
    second_size_ = b.size();
    second_data_ = (second_size_ > 0) ? const_cast<uint8_t*>(b.data()) : nullptr;
    HAL_I2C_Slave_Seq_Transmit_DMA(hi2c_, const_cast<uint8_t*>(a.data()), a.size(), I2C_NEXT_FRAME);
}

void hal_i2c_slave::receive(const std::span<uint8_t>& a)
{
    first_size_ = a.size();
    second_size_ = 0;
    second_data_ = nullptr;
    HAL_I2C_Slave_Seq_Receive_DMA(hi2c_, a.data(), a.size(), I2C_LAST_FRAME);
}

void hal_i2c_slave::receive(const std::span<uint8_t>& a, const std::span<uint8_t>& b)
{
    first_size_ = a.size();
    second_size_ = b.size();
    second_data_ = (second_size_ > 0) ? b.data() : nullptr;
    HAL_I2C_Slave_Seq_Receive_DMA(hi2c_, a.data(), a.size(), I2C_NEXT_FRAME);
}

size_t hal_i2c_slave::transferred_size(i2c::direction dir) const
{
    size_t size = first_size_;
    if (size > 0)
    {
        if (second_data_ == nullptr)
        {
            // second part is in progress (or there is none, and second_size_ is 0)
            size += second_size_;
        }
        // the DMA counter holds the remaining item count of the active part
        const auto* hdma = (dir == i2c::direction::WRITE) ? hi2c_->hdmarx : hi2c_->hdmatx;
        size -= __HAL_DMA_GET_COUNTER(hdma);
    }
    return size;
}

void hal_i2c_slave::handle_start(i2c::direction dir)
{
    bool success = has_module();
    if (success)
    {
        // on repeated START, the transferred size belongs to the previous direction
        size_t size = transferred_size(last_dir_);
        last_dir_ = dir;
        success = on_start(dir, size);
    }
    if (!success)
    {
        // impossible to NACK in read direction
        if (dir == i2c::direction::WRITE)
        {
            nack();
        }
        else
        {
            send_dummy();
        }
    }
}

void hal_i2c_slave::handle_tx_complete()
{
    if (second_data_ != nullptr)
    {
        auto* data = second_data_;
        second_data_ = nullptr;
        HAL_I2C_Slave_Seq_Transmit_DMA(hi2c_, data, second_size_, I2C_NEXT_FRAME);
    }
    else
    {
        send_dummy();
    }
}

void hal_i2c_slave::handle_rx_complete()
{
    if (second_data_ != nullptr)
    {
        auto* data = second_data_;
        second_data_ = nullptr;
        HAL_I2C_Slave_Seq_Receive_DMA(hi2c_, data, second_size_, I2C_LAST_FRAME);
    }
    else
    {
        nack();
    }
}

void hal_i2c_slave::handle_stop()
{
    if (has_module())
    {
        on_stop(last_dir_, transferred_size(last_dir_));
        first_size_ = 0;
        second_size_ = 0;
        second_data_ = nullptr;

        start_listen();
    }
}

void hal_i2c_slave::handle_error()
{
    // errors during listen mode (BERR, ARLO, OVR) keep the HAL in listen mode,
    // the transfer is concluded by the following STOP condition;
    // otherwise the transfer is over, and listening must be restarted
    if (HAL_I2C_GetState(hi2c_) == HAL_I2C_STATE_READY)
    {
        handle_stop();
    }
}
} // namespace i2c::stm32

using i2c::stm32::hal_i2c_slave;

// invoked for all HAL I2C handles, the ones not operated by this driver are ignored

extern "C" void HAL_I2C_AddrCallback(I2C_HandleTypeDef* hi2c, uint8_t TransferDirection,
                                     [[maybe_unused]] uint16_t AddrMatchCode)
{
    // I2C_DIRECTION_TRANSMIT is the master's write in both v1 and v2 IP HALs,
    // but its numeric value differs
    if (auto* slave = hal_i2c_slave::from_handle(hi2c); slave != nullptr)
    {
        slave->handle_start((TransferDirection == I2C_DIRECTION_TRANSMIT) ? i2c::direction::WRITE
                                                                          : i2c::direction::READ);
    }
}

extern "C" void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef* hi2c)
{
    if (auto* slave = hal_i2c_slave::from_handle(hi2c); slave != nullptr)
    {
        slave->handle_stop();
    }
}

extern "C" void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef* hi2c)
{
    if (auto* slave = hal_i2c_slave::from_handle(hi2c); slave != nullptr)
    {
        slave->handle_tx_complete();
    }
}

extern "C" void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef* hi2c)
{
    if (auto* slave = hal_i2c_slave::from_handle(hi2c); slave != nullptr)
    {
        slave->handle_rx_complete();
    }
}

extern "C" void HAL_I2C_ErrorCallback(I2C_HandleTypeDef* hi2c)
{
    if (auto* slave = hal_i2c_slave::from_handle(hi2c); slave != nullptr)
    {
        slave->handle_error();
    }
}
