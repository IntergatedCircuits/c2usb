// SPDX-License-Identifier: Apache-2.0
extern "C"
{
#include <FreeRTOS.h>
#include <board.h>
#include <httpsrv_freertos.h>
#include <lwip/netifapi.h>
#include <lwip/tcpip.h>
#include <pin_mux.h>
#if __has_include("fsl_spc.h")
#include <fsl_spc.h>
#endif
#include <task.h>
#include <usb_device_config.h>
#if __has_include("usb_phy.h")
#include <usb_phy.h>
#endif
#if __has_include("fsl_debug_console.h")
#include <fsl_debug_console.h>
#endif
}
#include <array>
#include <freertos/scheduler.hpp>
#include <usb/df/class/cdc_ncm.hpp>
#include <usb/df/config_factory.hpp>
#include <usb/df/device.hpp>
#include <usb/df/vendor/lwip/ncm_netif.hpp>
#include <usb/df/vendor/microsoft/os_extension.hpp>
#include <usb/df/vendor/nxp/mcux_mac.hpp>

constexpr usb::cdc::mac_address netif_mac{0x00, 0x00, 0x5e, 0x00, 0x53, 0x00};
constexpr usb::cdc::mac_address ncm_mac{0x00, 0x00, 0x5e, 0x00, 0x53, 0x01};
constexpr usb::product_info product{0x1FC9, "NXP", 0x00A2, "c2usb NCM HTTP server",
                                    usb::version("1.0")};
constexpr usb::speeds supported_speeds{usb::speed::FULL
#if USB_DEVICE_CONFIG_EHCI
                                       ,
                                       usb::speed::HIGH
#endif
};

auto& usb_mac()
{
    static std::array<uint8_t, 128> ctrl_msg_buffer{};
    static auto mac{
#if USB_DEVICE_CONFIG_EHCI
        usb::df::nxp::mcux_mac::ehci(ctrl_msg_buffer)
#elif USB_DEVICE_CONFIG_KHCI
        usb::df::nxp::mcux_mac::khci(ctrl_msg_buffer)
#endif
    };
    return mac;
}

auto& usb_device()
{
    static usb::df::microsoft::descriptors msos{};
    static usb::df::device_instance<supported_speeds> instance{usb_mac(), product, msos};
    return instance;
}

#if __has_include("fsl_spc.h")
extern "C" void BOARD_PowerMode_OD(void)
{
    spc_active_mode_dcdc_option_t dcdc_config{
        .DCDCVoltage = kSPC_DCDC_OverdriveVoltage,
        .DCDCDriveStrength = kSPC_DCDC_NormalDriveStrength,
    };
    SPC_SetActiveModeDCDCRegulatorConfig(SPC0, &dcdc_config);

    spc_sram_voltage_config_t sram_config{
        .operateVoltage = kSPC_sramOperateAt1P2V,
        .requestVoltageUpdate = true,
    };
    SPC_SetSRAMOperateVoltage(SPC0, &sram_config);
}
#endif

void app_thread(void*)
{
    using namespace usb::df;
    using namespace std::chrono_literals;

    static lwip::ncm_netif ncm_netif{netif_mac, freertos::thread::get_current()};

    using ntb_array = std::array<uint32_t, 1024>;
    C2USB_USB_TRANSFER_ALIGN(static ntb_array, ntb_in){};
    C2USB_USB_TRANSFER_ALIGN(static ntb_array, ntb_out){};
    static cdc::ncm::function ncm_function{ncm_mac, ncm_netif, {ntb_in, ntb_out}};

    ip4_addr_t address;
    ip4_addr_t netmask;
    ip4_addr_t gateway;
    IP4_ADDR(&address, 192, 0, 2, 1);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gateway, 0, 0, 0, 0);

    tcpip_init(nullptr, nullptr);
    if (netifapi_netif_add(ncm_netif.netif_handle(), &address, &netmask, &gateway, &ncm_netif,
                           &lwip::ncm_netif::init, &tcpip_input) != ERR_OK)
    {
        configASSERT(false);
    }
    (void)netifapi_netif_set_default(ncm_netif.netif_handle());
    (void)netifapi_netif_set_up(ncm_netif.netif_handle());

    http_server_enable_mdns(ncm_netif.netif_handle(), "usb-ncm");
    http_server_print_ip_cfg(ncm_netif.netif_handle());
    http_server_socket_init();

    static std::array<std::array<config::element, 7>, supported_speeds.count()> configurations{};
    for (const auto speed : supported_speeds)
    {
        configurations[supported_speeds.offset(speed)] = make_config(
            config::header(config::power::bus(200), "USB NCM"),
            ncm_function.config_entry(speed, usb::endpoint::address(0x02),
                                      usb::endpoint::address(0x82), usb::endpoint::address(0x81),
                                      usb::endpoint::interval::from_rate(speed, 8ms)));
        usb_device().set_config_for_speed(configurations[supported_speeds.offset(speed)], speed);
    }
    usb_device().open();

    ncm_netif.worker();
}

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
#if __has_include("fsl_debug_console.h")
    DbgConsole_Init();
#endif
}

extern "C" void USB_DeviceClockInit(void)
{
#if USB_DEVICE_CONFIG_EHCI
    usb_phy_config_struct_t phy_config{
        BOARD_USB_PHY_D_CAL,
        BOARD_USB_PHY_TXCAL45DP,
        BOARD_USB_PHY_TXCAL45DM,
    };
    SPC0->ACTIVE_VDELAY = 0x0500;
    SPC0->ACTIVE_CFG &= ~SPC_ACTIVE_CFG_CORELDO_VDD_DS_MASK;
    SPC0->ACTIVE_CFG |= SPC_ACTIVE_CFG_DCDC_VDD_LVL(0x3) | SPC_ACTIVE_CFG_CORELDO_VDD_LVL(0x3) |
                        SPC_ACTIVE_CFG_SYSLDO_VDD_DS_MASK | SPC_ACTIVE_CFG_DCDC_VDD_DS(0x2u);
    while (SPC0->SC & SPC_SC_BUSY_MASK)
    {
    }
    if ((SCG0->LDOCSR & SCG_LDOCSR_LDOEN_MASK) == 0)
    {
        SCG0->TRIM_LOCK = 0x5a5a0001U;
        SCG0->LDOCSR |= SCG_LDOCSR_LDOEN_MASK;
        while ((SCG0->LDOCSR & SCG_LDOCSR_VOUT_OK_MASK) == 0)
        {
        }
    }
    SYSCON->AHBCLKCTRLSET[2] |= SYSCON_AHBCLKCTRL2_USB_HS_MASK | SYSCON_AHBCLKCTRL2_USB_HS_PHY_MASK;
    SCG0->SOSCCFG &= ~(SCG_SOSCCFG_RANGE_MASK | SCG_SOSCCFG_EREFS_MASK);
    SCG0->SOSCCFG = (1U << SCG_SOSCCFG_RANGE_SHIFT) | (1U << SCG_SOSCCFG_EREFS_SHIFT);
    SCG0->SOSCCSR |= SCG_SOSCCSR_SOSCEN_MASK;
    while ((SCG0->SOSCCSR & SCG_SOSCCSR_SOSCVLD_MASK) == 0)
    {
    }
    SYSCON->CLOCK_CTRL |=
        SYSCON_CLOCK_CTRL_CLKIN_ENA_MASK | SYSCON_CLOCK_CTRL_CLKIN_ENA_FM_USBH_LPT_MASK;
    CLOCK_EnableClock(kCLOCK_UsbHs);
    CLOCK_EnableClock(kCLOCK_UsbHsPhy);
    CLOCK_EnableUsbhsPhyPllClock(kCLOCK_Usbphy480M, 24000000U);
    CLOCK_EnableUsbhsClock();
    USB_EhciPhyInit(usb_mac().controller_id(), BOARD_XTAL0_CLK_HZ, &phy_config);
    NVIC_SetPriority(USB1_HS_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
#else
    NVIC_SetPriority(USB0_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
#if FSL_FEATURE_USB_KHCI_IRC48M_MODULE_CLOCK_ENABLED
    CLOCK_EnableUsbfs0Clock(kCLOCK_UsbSrcIrc48M, 48000000U);
#else
    CLOCK_EnableUsbfs0Clock(kCLOCK_UsbSrcPll0, CLOCK_GetFreq(kCLOCK_PllFllSelClk));
#endif
#endif
}

#if USB_DEVICE_CONFIG_EHCI
extern "C" void USB1_HS_IRQHandler(void)
#elif USB_DEVICE_CONFIG_KHCI
extern "C" void USB0_IRQHandler(void)
#endif
{
    usb_mac().handle_irq();
    SDK_ISR_EXIT_BARRIER;
}

int main(void)
{
    BOARD_InitHardware();
    USB_DeviceClockInit();

    using namespace freertos;

    [[maybe_unused]] auto* main_thread =
        make_thread(&app_thread, nullptr, 3000, tskIDLE_PRIORITY + 3, "main");
    assert(main_thread != nullptr);
    scheduler::start();

    __builtin_unreachable();
}
