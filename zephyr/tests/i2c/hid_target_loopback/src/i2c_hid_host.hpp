// SPDX-License-Identifier: Apache-2.0
/*
 * HID over I2C loopback test:
 * two I2C peripherals of the chip are shorted, one is operated as the c2usb I2C HID device
 * (target, STM32 HAL driver with DMA), the other acts as the I2C HID host
 * (controller, Zephyr I2C driver with DMA).
 *
 * This header contains a minimal I2C HID host implementation, shared by the test applications.
 */
#pragma once

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <algorithm>
#include <array>
#include <span>

#include <i2c/hid/device.hpp>
#include <i2c/vendor/stm32/zephyr_i2c_slave.hpp>

#define I2C_HID_NODE DT_NODELABEL(i2c_hid)
#define CONTROLLER_NODE DT_NODELABEL(i2c_test_controller)
BUILD_ASSERT(DT_ON_BUS(I2C_HID_NODE, i2c_target), "i2c_hid node must be on an I2C target bus");

namespace test
{
using c2usb::le_uint16_t;

constexpr uint16_t TEST_ADDRESS = DT_REG_ADDR(I2C_HID_NODE);
constexpr uint16_t HID_DESC_REGISTER = DT_PROP(I2C_HID_NODE, hid_descr_addr);
constexpr k_timeout_t INT_TIMEOUT = K_MSEC(100);

inline const device* const controller = DEVICE_DT_GET(CONTROLLER_NODE);
inline const gpio_dt_spec int_pin = GPIO_DT_SPEC_GET(I2C_HID_NODE, int_gpios);

// I2C HID bus messages, as defined by the HID over I2C specification

/// @brief  Register address, written before reading a register's content.
struct register_request
{
    le_uint16_t reg;
};

/// @brief  Command without data.
struct command_request
{
    le_uint16_t command_register;
    i2c::hid::command<> command;
};

/// @brief  Command, followed by reading the data register.
template <::hid::report::id::type REPORT_ID>
struct data_command_request
{
    le_uint16_t command_register;
    i2c::hid::command<REPORT_ID> command;
    le_uint16_t data_register;
};

/// @brief  Report data with its length prefix, the length includes the prefix itself.
template <typename T>
struct report_data
{
    le_uint16_t length{sizeof(report_data)};
    T report{};
};

/// @brief  Command, followed by writing the data register.
template <typename T>
struct set_report_request
{
    le_uint16_t command_register;
    i2c::hid::command<T::ID> command;
    le_uint16_t data_register;
    report_data<T> data;
};

/// @brief  Output report written to the output register.
template <typename T>
struct output_request
{
    le_uint16_t output_register;
    report_data<T> data;
};

template <typename T>
std::span<const uint8_t> as_bytes(const T& obj)
{
    // hid-rp reports with a report ID aren't standard layout (the ID is in their base class),
    // so view the object representation, which is well-defined for trivially copyable types
    static_assert(std::is_trivially_copyable_v<T> and (alignof(T) == 1),
                  "bus messages must be packed, trivially copyable types");
    // NOLINTNEXTLINE(*-type-reinterpret-cast)
    return {reinterpret_cast<const uint8_t*>(&obj), sizeof(T)};
}

template <typename T>
std::span<uint8_t> as_writable_bytes(T& obj)
{
    static_assert(std::is_trivially_copyable_v<T> and (alignof(T) == 1),
                  "bus messages must be packed, trivially copyable types");
    // NOLINTNEXTLINE(*-type-reinterpret-cast)
    return {reinterpret_cast<uint8_t*>(&obj), sizeof(T)};
}

inline bool all_padding(std::span<const uint8_t> data)
{
    return std::ranges::all_of(data, [](uint8_t b) { return b == 0xff; });
}

/// @brief  Minimal I2C HID host implementation, on top of the Zephyr I2C controller API.
class hid_host
{
  public:
    template <typename T>
    static int write(const T& request)
    {
        auto data = as_bytes(request);
        return i2c_write(controller, data.data(), data.size(), TEST_ADDRESS);
    }

    /// @brief  Plain read, without register address: reads the input register.
    template <typename T>
    static int read(T& response)
    {
        auto data = as_writable_bytes(response);
        return i2c_read(controller, data.data(), data.size(), TEST_ADDRESS);
    }

    template <typename TReq, typename TResp>
    static int write_read(const TReq& request, TResp& response)
    {
        auto req = as_bytes(request);
        auto resp = as_writable_bytes(response);
        return i2c_write_read(controller, TEST_ADDRESS, req.data(), req.size(), resp.data(),
                              resp.size());
    }

    int read_hid_descriptor(i2c::hid::descriptor& desc)
    {
        int ret = write_read(register_request{HID_DESC_REGISTER}, desc);
        if (ret == 0)
        {
            desc_ = desc;
        }
        return ret;
    }

    template <typename T>
    int read_register(uint16_t reg, T& response)
    {
        return write_read(register_request{reg}, response);
    }

    int command(const i2c::hid::command<>& cmd)
    {
        return write(command_request{desc_.wCommandRegister, cmd});
    }

    /// @brief  GET_REPORT command for the report type T, in short or extended format
    ///         depending on its report ID.
    template <typename T>
    int get_report(report_data<T>& response)
    {
        return write_read(data_command_request<T::ID>{desc_.wCommandRegister,
                                                      i2c::hid::command<T::ID>(
                                                          i2c::hid::opcode::GET_REPORT, T::type()),
                                                      desc_.wDataRegister},
                          response);
    }

    /// @brief  SET_REPORT command for the report type T, in short or extended format
    ///         depending on its report ID.
    template <typename T>
    int set_report(const T& report)
    {
        return write(
            set_report_request<T>{desc_.wCommandRegister,
                                  i2c::hid::command<T::ID>(i2c::hid::opcode::SET_REPORT, T::type()),
                                  desc_.wDataRegister, report_data<T>{.report = report}});
    }

    template <typename T>
    int write_output(const T& report)
    {
        return write(output_request<T>{desc_.wOutputRegister, report_data<T>{.report = report}});
    }

    static bool int_asserted() { return gpio_pin_get_dt(&int_pin) > 0; }

    static bool wait_int(bool asserted, k_timeout_t timeout = INT_TIMEOUT)
    {
        k_timepoint_t end = sys_timepoint_calc(timeout);
        while (int_asserted() != asserted)
        {
            if (sys_timepoint_expired(end))
            {
                return false;
            }
            k_sleep(K_TICKS(1));
        }
        return true;
    }

    /// @brief  Performs the host side reset sequence, as done at enumeration.
    void reset()
    {
        zassert_ok(command(i2c::hid::command<>(i2c::hid::opcode::RESET)));
        zassert_true(wait_int(true), "INT not asserted after RESET");

        // the reset response is a zero length (sentinel) in the input register
        le_uint16_t sentinel{0xaaaa};
        zassert_ok(read(sentinel));
        zassert_equal(sentinel, 0, "invalid reset response");
        zassert_true(wait_int(false), "INT not deasserted after reset response");
    }

    /// @brief  Reads the HID descriptor and resets the device, as a host does at enumeration.
    void enumerate()
    {
        i2c::hid::descriptor desc{};
        zassert_ok(read_hid_descriptor(desc));
        reset();
    }

    [[nodiscard]] const i2c::hid::descriptor& descriptor() const { return desc_; }

  private:
    i2c::hid::descriptor desc_{};
};

/// @brief  Prepares the controller side of the bus.
inline void setup_bus()
{
    zassert_true(device_is_ready(controller), "I2C controller not ready");
    zassert_not_null(C2USB_I2C_SLAVE_DT_GET(I2C_HID_NODE), "I2C target driver not ready");
    zassert_ok(i2c_configure(controller, I2C_MODE_CONTROLLER | I2C_SPEED_SET(I2C_SPEED_STANDARD)));
}

/// @brief  Creates the I2C HID device for the application, once.
template <typename TApp>
i2c::hid::device& hid_device(TApp& app, const i2c::hid::product_info& pinfo)
{
    static auto buffer = i2c::hid::device::make_buffer<TApp::report_protocol>();
    static i2c::hid::device dev{app,
                                pinfo,
                                *C2USB_I2C_SLAVE_DT_GET(I2C_HID_NODE),
                                C2USB_I2C_HID_DT_ADDRESS(I2C_HID_NODE),
                                C2USB_I2C_HID_DT_DESCRIPTOR_REG(I2C_HID_NODE),
                                buffer};
    return dev;
}

} // namespace test
