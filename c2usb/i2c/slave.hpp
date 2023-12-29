/// @file
///
/// @author Benedek Kupper
/// @date   2023
///
/// @copyright
///         This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
///         If a copy of the MPL was not distributed with this file, You can obtain one at
///         https://mozilla.org/MPL/2.0/.
///
#ifndef __I2C_SLAVE_HPP_
#define __I2C_SLAVE_HPP_

#include "i2c/base.hpp"

namespace i2c
{
/// @brief  The slave class is an abstract base for I2C slave transport drivers.
class slave : public polymorphic
{
  public:
    /// @brief  The module class is a callback interface for I2C slave functionality
    ///         at a specific address.
    class module : public interface
    {
      public:
        virtual bool on_start(direction dir, size_t data_length) = 0;
        virtual void on_stop(direction dir, size_t data_length) = 0;
    };

    bool has_module([[maybe_unused]] address slave_addr) const { return module_ != nullptr; }

    /// @brief  Registers a slave module on the I2C bus.
    /// @param  m: module pointer
    /// @param  slave_addr: the I2C slave address to listen to
    /// @return true if module registered
    bool register_module(module* m, address slave_addr)
    {
        // single module design at the moment
        if (module_ != nullptr)
        {
            return false;
        }
        module_ = m;
        start_listen(slave_addr);
        return true;
    }

    /// @brief  Unregisters the active module from the I2C bus interface.
    /// @param  m: module pointer
    /// @return true if module unregistered
    bool unregister_module(module* m)
    {
        // single module design at the moment
        if (module_ != m)
        {
            return false;
        }
        stop_listen();
        module_ = nullptr;
        return true;
    }

    /// @brief  This call changes the signal level of the INT line,
    ///         that requests transfers from the I2C master.
    /// @param  asserted: whether the line is active (active low signal)
    virtual void set_pin_interrupt(bool asserted) = 0;

    /// @brief  Send a single span of data.
    /// @param  a: the data to send
    virtual void send(const std::span<const uint8_t>& a) = 0;

    /// @brief  Send up to two spans of data in one transfer.
    /// @param  a: first data span
    /// @param  a: second data span
    virtual void send(const std::span<const uint8_t>& a, const std::span<const uint8_t>& b) = 0;

    template <typename T>
    void send(const T* a)
        requires(std::is_trivially_copyable_v<T>)
    {
        send(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(a), sizeof(T)));
    }
    template <typename T>
    void send(const T* a, const std::span<const uint8_t>& b)
        requires(std::is_trivially_copyable_v<T>)
    {
        send(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(a), sizeof(T)), b);
    }

    /// @brief  Receive a single span of data.
    /// @param  a: the data to receive
    virtual void receive(const std::span<uint8_t>& a) = 0;

    /// @brief  Receive up to two spans of data in one transfer.
    /// @param  a: first data span
    /// @param  a: second data span
    virtual void receive(const std::span<uint8_t>& a, const std::span<uint8_t>& b) = 0;

    template <typename T>
    void receive(T* a)
        requires(std::is_trivially_copyable_v<T>)
    {
        receive(std::span<uint8_t>(reinterpret_cast<uint8_t*>(a), sizeof(T)));
    }
    template <typename T>
    void receive(T* a, const std::span<uint8_t>& b)
        requires(std::is_trivially_copyable_v<T>)
    {
        receive(std::span<uint8_t>(reinterpret_cast<uint8_t*>(a), sizeof(T)), b);
    }

  protected:
    constexpr slave() = default;

    /// @brief  Start listening to I2C transfers
    /// @param  slave_addr: the I2C address to listen on
    virtual void start_listen(address slave_addr) = 0;

    /// @brief  Stop listening to I2C transfers
    virtual void stop_listen() = 0;

    /// @brief  Call the module to handle I2C (re)start events.
    /// @param  dir: the transfer's direction
    /// @param  data_length: the amount of data transferred
    ///         since the last START/STOP
    /// @return If false is returned, the module rejects the transfer.
    ///         Send a NACK or send dummy bytes until NACKed.
    bool on_start(direction dir, size_t data_length)
    {
        assert(module_);
        return module_->on_start(dir, data_length);
    }

    /// @brief  Call the module to handle I2C stop events.
    /// @param  dir: the transfer's direction
    /// @param  data_length: the amount of data transferred since the last START
    void on_stop(direction dir, size_t data_length)
    {
        assert(module_);
        return module_->on_stop(dir, data_length);
    }

  private:
    module* module_{};
};
} // namespace i2c

#endif // __I2C_SLAVE_HPP_
