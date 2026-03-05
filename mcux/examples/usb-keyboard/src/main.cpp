extern "C"
{
#include "board.h"
#include "fsl_gpio.h"
#include "pin_mux.h"

#if defined(FSL_FEATURE_SOC_PORT_COUNT) && (FSL_FEATURE_SOC_PORT_COUNT)
#include "fsl_port.h"
#endif
}
#include "port/nxp/mcux_mac.hpp"
#include "simple_keyboard.hpp"
#include "usb/df/class/hid.hpp"
#include "usb/df/device.hpp"

auto& mac()
{
    static std::array<uint8_t, 128> buffer;
    static auto mac{usb::df::nxp::mcux_mac::khci(buffer)};
    return mac;
}

extern "C" void USB0_IRQHandler(void)
{
    mac().handle_irq();
    SDK_ISR_EXIT_BARRIER;
}

extern "C" void BOARD_InitBootPins(void);
extern "C" void BOARD_InitHardware(void)
{
    BOARD_InitBootClocks();
    BOARD_InitBootPins();
    BOARD_InitLEDsPins();
    BOARD_InitBUTTONsPins();
#if (defined(FSL_FEATURE_PORT_HAS_NO_INTERRUPT) && FSL_FEATURE_PORT_HAS_NO_INTERRUPT) ||           \
    (!defined(FSL_FEATURE_SOC_PORT_COUNT))
    GPIO_SetPinInterruptConfig(BOARD_SW2_GPIO, BOARD_SW2_GPIO_PIN, kGPIO_InterruptEitherEdge);
#else
    PORT_SetPinInterruptConfig(BOARD_SW2_PORT, BOARD_SW2_GPIO_PIN, kPORT_InterruptEitherEdge);
#endif
    EnableIRQ(BOARD_SW2_IRQ);
    // BOARD_InitDebugConsole();
}

extern "C" void USB_DeviceClockInit(void)
{
#if ((defined FSL_FEATURE_USB_KHCI_IRC48M_MODULE_CLOCK_ENABLED) &&                                 \
     (FSL_FEATURE_USB_KHCI_IRC48M_MODULE_CLOCK_ENABLED))
    CLOCK_EnableUsbfs0Clock(kCLOCK_UsbSrcIrc48M, 48000000U);
#else
    CLOCK_EnableUsbfs0Clock(kCLOCK_UsbSrcPll0, CLOCK_GetFreq(kCLOCK_PllFllSelClk));
#endif /* FSL_FEATURE_USB_KHCI_IRC48M_MODULE_CLOCK_ENABLED */
}

auto& keyboard_app()
{
    static simple_keyboard<> kb(
        [](const simple_keyboard<>::kb_leds_report& report)
        {
            GPIO_PinWrite(BOARD_LED_RED_GPIO, BOARD_LED_RED_GPIO_PIN,
                          !report.leds.test(hid::page::leds::CAPS_LOCK));
        });
    return kb;
}

auto& device()
{
    static constexpr usb::product_info prinfo{0x1FC9, "NXP", 0x0091, "c2usb keyboard",
                                              usb::version("1.0")};
    static usb::df::device_instance<usb::speed::FULL> device{mac(), prinfo};
    return device;
}

extern "C" void BOARD_SW2_IRQ_HANDLER(void)
{
#if (defined(FSL_FEATURE_PORT_HAS_NO_INTERRUPT) && FSL_FEATURE_PORT_HAS_NO_INTERRUPT) ||           \
    (!defined(FSL_FEATURE_SOC_PORT_COUNT))
    /* Clear external interrupt flag. */
    GPIO_GpioClearInterruptFlags(BOARD_SW2_GPIO, 1U << BOARD_SW2_GPIO_PIN);
#else
    /* Clear external interrupt flag. */
    GPIO_PortClearInterruptFlags(BOARD_SW2_GPIO, 1U << BOARD_SW2_GPIO_PIN);
#endif
    bool pressed = GPIO_PinRead(BOARD_SW2_GPIO, BOARD_SW2_GPIO_PIN) == 0;
    switch (device().power_state())
    {
    case usb::power::state::L2_SUSPEND:
        if (pressed)
        {
            device().remote_wakeup();
        }
        break;
    case usb::power::state::L0_ON:
        keyboard_app().send_key(hid::page::keyboard_keypad::KEYBOARD_CAPS_LOCK, pressed);
        break;
    default:
        break;
    }
    SDK_ISR_EXIT_BARRIER;
}

int main(void)
{
    BOARD_InitHardware();
    USB_DeviceClockInit();

    static usb::df::hid::function usb_kb{keyboard_app(), usb::hid::boot_protocol_mode::KEYBOARD};
    static const auto single_config = usb::df::config::make_config(
        usb::df::config::header(usb::df::config::power::bus(100, true), "usb-keyboard"),
        usb::df::hid::config(usb_kb, usb::speed::FULL, usb::endpoint::address(0x81), 1
#if 0 // optional dedicated interrupt endpoint for LED output report
                             ,
                             usb::endpoint::address(0x01), 20
#endif
                             ));
    device().set_config(single_config);
    device().open();
    device().set_power_event_delegate(
        [](usb::df::device& dev, usb::df::device::event ev)
        {
            GPIO_PinWrite(BOARD_LED_GREEN_GPIO, BOARD_LED_GREEN_GPIO_PIN,
                          dev.power_state() != usb::power::state::L0_ON);
        });

    while (true)
    {
        __WFI();
    }
}
