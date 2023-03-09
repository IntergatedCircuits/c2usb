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
    /// @brief  The slave class is an abstract base for I2C slave
    ///         transport drivers.
    class slave
    {
    public:
        virtual ~slave() = default;

        bool has_module() const { return module_ != nullptr; }

        /// @brief  Registers a slave module on the I2C bus.
        /// @tparam T: module type
        /// @tparam ON_START: callback for I2C (re)start events
        /// @tparam ON_STOP: callback for I2C stop events
        /// @param  module: module pointer
        /// @param  slave_addr: the I2C slave address to listen to
        template <typename T,
            bool(T::*ON_START)(direction, size_t),
            void(T::*ON_STOP)(direction, size_t)>
        void register_module(T* module, address slave_addr)
        {
            assert(not has_module());
            on_start_ = [](void* m, direction dir, size_t s) {
                T* p = static_cast<T*>(m);
                return (p->*ON_START)(dir, s);
            };
            on_stop_ = [](void* m, direction dir, size_t s) {
                T* p = static_cast<T*>(m);
                (p->*ON_STOP)(dir, s);
            };
            module_ = static_cast<decltype(module_)>(module);

            // single module use case
            start_listen(slave_addr);
        }

        /// @brief  Unregisters the active module from the I2C bus interface.
        /// @tparam T: module type
        /// @param  module: module pointer
        template<class T>
        void unregister_module(T* module)
        {
            if (module_ == static_cast<void*>(module))
            {
                // single module use case, just shut down entirely
                stop_listen();

                module_ = nullptr;
                on_start_ = nullptr;
                on_stop_ = nullptr;
            }
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
            assert(has_module());
            return on_start_(module_, dir, data_length);
        }

        /// @brief  Call the module to handle I2C stop events.
        /// @param  dir: the transfer's direction
        /// @param  data_length: the amount of data transferred since the last START
        void on_stop(direction dir, size_t data_length)
        {
            assert(has_module());
            return on_stop_(module_, dir, data_length);
        }

        slave(const slave&) = delete;
        slave& operator=(const slave&) = delete;

    private:
        void* module_ {};
        bool(*on_start_)(void *module, direction dir, size_t data_length) {};
        void(*on_stop_)(void *module, direction dir, size_t data_length) {};
    };
}

#endif // __I2C_SLAVE_HPP_
