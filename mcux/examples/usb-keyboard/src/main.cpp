extern "C"
{
#include "board.h"
#include "fsl_gpio.h"
#include "pin_mux.h"

#if FSL_FEATURE_SOC_PORT_COUNT
#include "fsl_port.h"
#endif
#if __has_include("usb_phy.h")
#include "usb_phy.h"
#endif
}
#include "port/nxp/mcux_mac.hpp"
#include "simple_keyboard.hpp"
#include "usb/df/class/hid.hpp"
#include "usb/df/device.hpp"

auto& mac()
{
    static std::array<uint8_t, 128> ctrl_msg_buffer;
    static auto mac{
#if USB_DEVICE_CONFIG_EHCI
        usb::df::nxp::mcux_mac::ehci(ctrl_msg_buffer)
#elif USB_DEVICE_CONFIG_KHCI
        usb::df::nxp::mcux_mac::khci(ctrl_msg_buffer)
#endif
    };
    return mac;
}

#if USB_DEVICE_CONFIG_EHCI
extern "C" void USB1_HS_IRQHandler(void)
#elif USB_DEVICE_CONFIG_KHCI
extern "C" void USB0_IRQHandler(void)
#endif
{
    mac().handle_irq();
    SDK_ISR_EXIT_BARRIER;
}

extern "C" void BOARD_InitBootPins(void);

extern "C" void BOARD_InitHardware(void)
{
#if USB_DEVICE_CONFIG_EHCI
    /* attach FRO 12M to FLEXCOMM4 (debug console) */
    CLOCK_SetClkDiv(kCLOCK_DivFlexcom4Clk, 1u);
    CLOCK_AttachClk(BOARD_DEBUG_UART_CLK_ATTACH);

    /* enable clock for GPIO*/
    CLOCK_EnableClock(kCLOCK_Gpio0);
    CLOCK_EnableClock(kCLOCK_Gpio4);

    BOARD_InitPins();
    BOARD_PowerMode_OD();
    BOARD_InitBootClocks();

#elif USB_DEVICE_CONFIG_KHCI
    BOARD_InitBootClocks();
    BOARD_InitBootPins();
#endif

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
#if USB_DEVICE_CONFIG_EHCI
    usb_phy_config_struct_t phyConfig = {
        BOARD_USB_PHY_D_CAL,
        BOARD_USB_PHY_TXCAL45DP,
        BOARD_USB_PHY_TXCAL45DM,
    };
    SPC0->ACTIVE_VDELAY = 0x0500;
    /* Change the power DCDC to 1.8v (By deafult, DCDC is 1.8V), CORELDO to 1.1v (By deafult,
     * CORELDO is 1.0V) */
    SPC0->ACTIVE_CFG &= ~SPC_ACTIVE_CFG_CORELDO_VDD_DS_MASK;
    SPC0->ACTIVE_CFG |= SPC_ACTIVE_CFG_DCDC_VDD_LVL(0x3) | SPC_ACTIVE_CFG_CORELDO_VDD_LVL(0x3) |
                        SPC_ACTIVE_CFG_SYSLDO_VDD_DS_MASK | SPC_ACTIVE_CFG_DCDC_VDD_DS(0x2u);
    /* Wait until it is done */
    while (SPC0->SC & SPC_SC_BUSY_MASK)
        ;
    if (0u == (SCG0->LDOCSR & SCG_LDOCSR_LDOEN_MASK))
    {
        SCG0->TRIM_LOCK = 0x5a5a0001U;
        SCG0->LDOCSR |= SCG_LDOCSR_LDOEN_MASK;
        /* wait LDO ready */
        while (0U == (SCG0->LDOCSR & SCG_LDOCSR_VOUT_OK_MASK))
            ;
    }
    SYSCON->AHBCLKCTRLSET[2] |= SYSCON_AHBCLKCTRL2_USB_HS_MASK | SYSCON_AHBCLKCTRL2_USB_HS_PHY_MASK;
    SCG0->SOSCCFG &= ~(SCG_SOSCCFG_RANGE_MASK | SCG_SOSCCFG_EREFS_MASK);
    /* xtal = 20 ~ 30MHz */
    SCG0->SOSCCFG = (1U << SCG_SOSCCFG_RANGE_SHIFT) | (1U << SCG_SOSCCFG_EREFS_SHIFT);
    SCG0->SOSCCSR |= SCG_SOSCCSR_SOSCEN_MASK;
    while (1)
    {
        if (SCG0->SOSCCSR & SCG_SOSCCSR_SOSCVLD_MASK)
        {
            break;
        }
    }
    SYSCON->CLOCK_CTRL |=
        SYSCON_CLOCK_CTRL_CLKIN_ENA_MASK | SYSCON_CLOCK_CTRL_CLKIN_ENA_FM_USBH_LPT_MASK;
    CLOCK_EnableClock(kCLOCK_UsbHs);
    CLOCK_EnableClock(kCLOCK_UsbHsPhy);
    CLOCK_EnableUsbhsPhyPllClock(kCLOCK_Usbphy480M, 24000000U);
    CLOCK_EnableUsbhsClock();
    USB_EhciPhyInit(mac().controller_id(), BOARD_XTAL0_CLK_HZ, &phyConfig);

#elif FSL_FEATURE_USB_KHCI_IRC48M_MODULE_CLOCK_ENABLED
    CLOCK_EnableUsbfs0Clock(kCLOCK_UsbSrcIrc48M, 48000000U);
#else
    CLOCK_EnableUsbfs0Clock(kCLOCK_UsbSrcPll0, CLOCK_GetFreq(kCLOCK_PllFllSelClk));
#endif
}

auto& keyboard_app()
{
    static simple_keyboard<[](keyboard_leds_data leds)
                           {
                               GPIO_PinWrite(BOARD_LED_RED_GPIO, BOARD_LED_RED_GPIO_PIN,
                                             !leds.test(hid::page::leds::CAPS_LOCK));
                           }>
        kb{};
    return kb;
}

auto& device()
{
    static constexpr usb::product_info prinfo{0x1FC9, "NXP", 0x0091, "c2usb keyboard",
                                              usb::version("1.0")};
    static usb::df::device_instance<
#if USB_DEVICE_CONFIG_EHCI
        usb::speeds(usb::speed::FULL, usb::speed::HIGH)
#else
        usb::speed::FULL
#endif
        >
        device{mac(), prinfo};
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

    /* Define the init structure for the output LED pin */
    gpio_pin_config_t led_config = {
        kGPIO_DigitalOutput,
        0,
    };
    GPIO_PinInit(BOARD_LED_RED_GPIO, BOARD_LED_RED_GPIO_PIN, &led_config);
    GPIO_PinInit(BOARD_LED_GREEN_GPIO, BOARD_LED_GREEN_GPIO_PIN, &led_config);

    static usb::df::hid::function usb_kb{keyboard_app(), usb::hid::boot_protocol_mode::KEYBOARD};
    static const auto single_config = usb::df::config::make_config(
        usb::df::config::header(usb::df::config::power::bus(100, true), "usb-keyboard"),
        usb::df::hid::config(usb_kb, usb::speed::FULL, usb::endpoint::address(0x81), 1
#if 0 // optional dedicated interrupt endpoint for LED output report
                             ,
                             usb::endpoint::address(0x01), 20
#endif
                             ));

    device().set_config_for_speed(single_config, usb::speed::FULL);
#if USB_DEVICE_CONFIG_EHCI
    device().set_config_for_speed(single_config, usb::speed::HIGH);
#endif

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
