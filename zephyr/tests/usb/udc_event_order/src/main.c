/*
 * Copyright (c) 2026 The c2usb contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <stm32_ll_bus.h>
#include <stm32_ll_pwr.h>
#include <stm32h7xx_ll_usb.h>

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/usb/uhc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(udc_event_order_host, LOG_LEVEL_INF);

#define USB_CLOCK_NODE DT_NODELABEL(usbotg_fs)
#define UHC_NODE DT_NODELABEL(uhc_host)
#define VBUS_ENABLE_GPIO_NODE DT_NODELABEL(gpiod)
#define VBUS_SENSE_GPIO_NODE DT_NODELABEL(gpioa)
#define VBUS_OVERCURRENT_GPIO_NODE DT_NODELABEL(gpiog)
#define VBUS_SENSE_PIN 9
#define VBUS_ENABLE_PIN 10
#define VBUS_OVERCURRENT_PIN 7

PINCTRL_DT_DEFINE(USB_CLOCK_NODE);

static const struct device* const uhc_dev = DEVICE_DT_GET(UHC_NODE);
static const struct device* const clock_dev = DEVICE_DT_GET(DT_CLOCKS_CTLR(USB_CLOCK_NODE));
static const struct device* const vbus_enable_gpio_dev =
    DEVICE_DT_GET(VBUS_ENABLE_GPIO_NODE);
static const struct device* const vbus_sense_gpio_dev =
    DEVICE_DT_GET(VBUS_SENSE_GPIO_NODE);
static const struct device* const vbus_overcurrent_gpio_dev =
    DEVICE_DT_GET(VBUS_OVERCURRENT_GPIO_NODE);
static const struct stm32_pclken usb_clocks[] = STM32_DT_CLOCKS(USB_CLOCK_NODE);
static const struct pinctrl_dev_config* const usb_pinctrl_cfg =
    PINCTRL_DT_DEV_CONFIG_GET(USB_CLOCK_NODE);
static bool host_initialized;

static int host_event_callback(const struct device* dev, const struct uhc_event* event)
{
    ARG_UNUSED(dev);

    LOG_INF("UHC event %d status %d", event->type, event->status);

    return 0;
}

static int host_initialize(const struct shell* shell)
{
    int ret;

    if (host_initialized)
    {
        shell_print(shell, "OK already initialized");
        return 0;
    }

    if (!device_is_ready(uhc_dev) || !device_is_ready(clock_dev) ||
        !device_is_ready(vbus_enable_gpio_dev) || !device_is_ready(vbus_sense_gpio_dev) ||
        !device_is_ready(vbus_overcurrent_gpio_dev))
    {
        shell_error(shell, "UHC, clock, or GPIO controller is not ready");
        return -ENODEV;
    }

    ret = pinctrl_apply_state(usb_pinctrl_cfg, PINCTRL_STATE_DEFAULT);
    if (ret != 0)
    {
        shell_error(shell, "Failed to configure USB pins: %d", ret);
        return ret;
    }

    ret = gpio_pin_configure(vbus_enable_gpio_dev, VBUS_ENABLE_PIN, GPIO_OUTPUT_ACTIVE);
    if (ret != 0)
    {
        shell_error(shell, "Failed to configure VBUS enable: %d", ret);
        return ret;
    }

    ret = gpio_pin_configure(vbus_sense_gpio_dev, VBUS_SENSE_PIN, GPIO_INPUT);
    if (ret != 0)
    {
        shell_error(shell, "Failed to configure VBUS sense: %d", ret);
        return ret;
    }

    ret = gpio_pin_configure(vbus_overcurrent_gpio_dev, VBUS_OVERCURRENT_PIN, GPIO_INPUT);
    if (ret != 0)
    {
        shell_error(shell, "Failed to configure overcurrent input: %d", ret);
        return ret;
    }

    LL_PWR_EnableUSBVoltageDetector();
    LL_PWR_DisableUSBReg();
    for (uint32_t elapsed_us = 0U; elapsed_us < 100000U; elapsed_us += 100U)
    {
        if (LL_PWR_IsActiveFlag_USB() != 0U)
        {
            break;
        }
        k_busy_wait(100U);
    }
    if (LL_PWR_IsActiveFlag_USB() == 0U)
    {
        shell_error(shell, "USB supply did not become ready");
        return -ETIMEDOUT;
    }
    ret = clock_control_configure(clock_dev, (clock_control_subsys_t)&usb_clocks[1], NULL);
    if (ret != 0)
    {
        shell_error(shell, "Failed to select USB clock: %d", ret);
        return ret;
    }

    ret = clock_control_on(clock_dev, (clock_control_subsys_t)&usb_clocks[0]);
    if (ret != 0)
    {
        shell_error(shell, "Failed to enable USB clock: %d", ret);
        return ret;
    }

    LL_AHB1_GRP1_DisableClockSleep(LL_AHB1_GRP1_PERIPH_USB2OTGHSULPI);
    USB_OTG_FS->GUSBCFG |= USB_OTG_GUSBCFG_PHYSEL;

    ret = uhc_init(uhc_dev, host_event_callback, NULL);
    if (ret != 0)
    {
        shell_error(shell, "UHC init failed: %d", ret);
        return ret;
    }

    ret = uhc_enable(uhc_dev);
    if (ret != 0)
    {
        shell_error(shell, "UHC enable failed: %d", ret);
        return ret;
    }

    ret = uhc_sof_enable(uhc_dev);
    if (ret != 0)
    {
        shell_error(shell, "SOF enable failed: %d", ret);
        return ret;
    }

    host_initialized = true;
    shell_print(shell, "OK initialized VBUS=off");

    return 0;
}

static int host_set_vbus(const struct shell* shell, bool enable)
{
    int ret;
    int overcurrent;
    int vbus_sense;
    int vbus_enable;

    if (!host_initialized)
    {
        shell_error(shell, "Run 'host init' first");
        return -EPERM;
    }

    ret = gpio_pin_set(vbus_enable_gpio_dev, VBUS_ENABLE_PIN, enable ? 0 : 1);
    if (ret != 0)
    {
        shell_error(shell, "Failed to set VBUS: %d", ret);
        return ret;
    }

    if (enable)
    {
        k_sleep(K_MSEC(10));
    }
    overcurrent = gpio_pin_get(vbus_overcurrent_gpio_dev, VBUS_OVERCURRENT_PIN);
    vbus_sense = gpio_pin_get(vbus_sense_gpio_dev, VBUS_SENSE_PIN);
    vbus_enable = gpio_pin_get(vbus_enable_gpio_dev, VBUS_ENABLE_PIN);
    shell_print(shell, "OK VBUS=%s EN_RAW=%d VBUS_SENSE_RAW=%d OC_RAW=%d",
                enable ? "on" : "off", vbus_enable, vbus_sense, overcurrent);

    return 0;
}

static int host_bus_operation(const struct shell* shell, const char* name,
                              int (*operation)(const struct device* dev))
{
    int ret;

    if (!host_initialized)
    {
        shell_error(shell, "Run 'host init' first");
        return -EPERM;
    }

    ret = operation(uhc_dev);
    if (ret != 0)
    {
        shell_error(shell, "%s request failed: %d", name, ret);
        return ret;
    }

    shell_print(shell, "OK %s requested", name);
    return 0;
}

static int cmd_host(const struct shell* shell, size_t argc, char** argv)
{
    if (argc == 2 && strcmp(argv[1], "init") == 0)
    {
        return host_initialize(shell);
    }

    if (argc == 3 && strcmp(argv[1], "vbus") == 0)
    {
        if (strcmp(argv[2], "on") == 0)
        {
            return host_set_vbus(shell, true);
        }
        if (strcmp(argv[2], "off") == 0)
        {
            return host_set_vbus(shell, false);
        }
    }

    if (argc == 2 && strcmp(argv[1], "reset") == 0)
    {
        return host_bus_operation(shell, "reset", uhc_bus_reset);
    }

    if (argc == 2 && strcmp(argv[1], "suspend") == 0)
    {
        return host_bus_operation(shell, "suspend", uhc_bus_suspend);
    }

    if (argc == 2 && strcmp(argv[1], "resume") == 0)
    {
        return host_bus_operation(shell, "resume", uhc_bus_resume);
    }

    shell_error(shell, "Usage: host init|vbus on|vbus off|reset|suspend|resume");
    return -EINVAL;
}

SHELL_CMD_ARG_REGISTER(host, NULL, "Control the USB host test fixture", cmd_host, 2, 1);

int main(void)
{
    return 0;
}
