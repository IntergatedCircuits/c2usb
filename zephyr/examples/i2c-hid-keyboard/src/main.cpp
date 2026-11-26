// SPDX-License-Identifier: Apache-2.0
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "../../iolib.hpp"
#include <hid/example/simple_keyboard.hpp>
#include <i2c/hid/device.hpp>
#include <i2c/vendor/stm32/zephyr_i2c_slave.hpp>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

// the "hid-over-i2c" compatible is also used for remote devices on the controller side,
// make sure that the node describes this device, under an I2C target controller
#define I2C_HID_NODE DT_NODELABEL(i2c_hid)
BUILD_ASSERT(DT_ON_BUS(I2C_HID_NODE, i2c_target), "i2c_hid node must be on an I2C target bus");

static const uint8_t caps_led = 0;
static const gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static gpio_callback button_cb_data;

/// @brief  Button edge interrupt handler, sends the key state change directly from ISR context.
///         The GPIO interrupt must not preempt the I2C and DMA interrupts (its priority must be
///         numerically equal or higher), so that it behaves as the application context
///         towards the I2C HID device.
///         No debouncing is applied, only the level changes are reported.
static void button_isr(const device*, gpio_callback*, gpio_port_pins_t)
{
    static bool pressed{};
    bool now = gpio_pin_get_dt(&button) > 0;
    if (now != pressed)
    {
        pressed = now;
        simple_keyboard::instance().send_key(hid::page::keyboard_keypad::KEYBOARD_CAPS_LOCK,
                                             pressed);
    }
}

static int button_init()
{
    if (!gpio_is_ready_dt(&button))
    {
        return -ENODEV;
    }
    int ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (ret == 0)
    {
        gpio_init_callback(&button_cb_data, button_isr, BIT(button.pin));
        ret = gpio_add_callback_dt(&button, &button_cb_data);
    }
    if (ret == 0)
    {
        ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
    }
    return ret;
}

int main(void)
{
    auto* slave = C2USB_I2C_SLAVE_DT_GET(I2C_HID_NODE);
    if (slave == nullptr)
    {
        LOG_ERR("I2C target driver not ready");
        return -ENODEV;
    }

    simple_keyboard::instance().set_leds_callback(
        [](keyboard_leds_data leds)
        { board_leds::set(caps_led, leds.test(hid::page::leds::CAPS_LOCK)); });

    // vendor and product ID are inherited from USB,
    // version indicates product HW / SW version
    static constexpr i2c::hid::product_info product_info{
        CONFIG_DEMO_MANUFACTURER_ID, CONFIG_DEMO_PRODUCT_ID, i2c::hid::version("1.0")};

    static auto i2c_hid_buffer = i2c::hid::device::make_buffer<simple_keyboard::report_protocol>();
    static i2c::hid::device i2c_hid_dev{simple_keyboard::instance(),
                                        product_info,
                                        *slave,
                                        C2USB_I2C_HID_DT_ADDRESS(I2C_HID_NODE),
                                        C2USB_I2C_HID_DT_DESCRIPTOR_REG(I2C_HID_NODE),
                                        i2c_hid_buffer};

    int ret = button_init();
    if (ret != 0)
    {
        LOG_ERR("Button init failed: %d", ret);
        return ret;
    }

    LOG_INF("I2C HID device listening at address 0x%02x", DT_REG_ADDR(I2C_HID_NODE));

    // everything is interrupt driven from here on, but main must not return:
    // without multithreading that would lock the interrupts
    while (true)
    {
#if CONFIG_MULTITHREADING
        k_sleep(K_FOREVER);
#else
        k_cpu_idle();
#endif
    }
}
