#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/ztest.h>

#include <algorithm>
#include <ranges>
#include <string_view>

#include <hid/example/simple_keyboard.hpp>
#include <usb/descriptor_set.hpp>
#include <usb/df/class/cdc_acm.hpp>
#include <usb/df/class/dfu.hpp>
#include <usb/df/class/hid.hpp>
#include <usb/df/config.hpp>
#include <usb/df/device.hpp>
#include <usb/df/vendor/zephyr/shell.hpp>
#include <usb/df/vendor/zephyr/udc_mac.hpp>
#include <usb/product_info.hpp>
#include <usb/standard/descriptors.hpp>
#include <usb/standard/requests.hpp>
#include <usb_host.hpp>
#include <zephyr/thread.hpp>

extern "C"
{
#define class class_
#include <usbh_ch9.h>
#include <usbh_device.h>
#include <zephyr/usb/usbh.h>
#undef class
}

LOG_MODULE_REGISTER(c2usb_usb_device_virtual, LOG_LEVEL_DBG);

using namespace zephyr;
using namespace std::chrono_literals;

constexpr uint16_t TEST_VID = 0x2fe3;
constexpr uint16_t TEST_PID = 0x1201;
constexpr uint16_t TEST_LANG_ID = 0x0409;

static constexpr usb::product_info product_info{TEST_VID, "C2USB", TEST_PID, "C2USB Loop Test",
                                                usb::version("1.0")};

static unsigned dfu_detach_req_count = 0;
auto& dfu_runtime_fn()
{
    static usb::df::dfu::runtime_function fn{"DFU Runtime", [](std::chrono::milliseconds)
                                             {
                                                 dfu_detach_req_count++;
                                                 LOG_DBG("DFU DETACH request received, count=%u",
                                                         dfu_detach_req_count);
                                             }};
    return fn;
}

template <usb::speed SPEED>
const auto& loop_config(const char* name)
{
    const auto config_header = usb::df::config::header(usb::df::config::power::bus(100), name);

    static const auto cfg = usb::df::config::make_config(
        config_header,
        usb::df::zephyr::shell::handle().config_entry(SPEED, usb::endpoint::address(0x01),
                                                      usb::endpoint::address(0x81),
                                                      usb::endpoint::address(0x8f)),
        dfu_runtime_fn().config_entry());

    return cfg;
}

auto& mac()
{
    constexpr uint16_t CTRL_EP_BUF_SIZE = 512;
    static usb::df::zephyr::udc_mac loop_mac{DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
                                             CTRL_EP_BUF_SIZE};
    return loop_mac;
}

auto& loop_device()
{
    static std::optional<usb::df::device_instance<usb::speeds(usb::speed::FULL, usb::speed::HIGH)>>
        dev_opt;
    return dev_opt;
}

static void test_device_string(usb::test::device* dev, uint8_t index, std::string_view expected)
{
    auto string_desc = dev->control_in<usb::standard::descriptor::string>(
        {usb::standard::device::GET_DESCRIPTOR,
         uint8_t(usb::standard::descriptor::type::STRING) << 8 | index, TEST_LANG_ID, 255});

    zassert_true(string_desc.has_data(), "Failed to read string descriptor %d", index);
    zassert_true(string_desc->type_valid(), "String descriptor %d has invalid type", index);

    bool match = std::ranges::equal(string_desc->u16string(), expected, {}, {},
                                    [](char ch) { return static_cast<char16_t>(ch); });

    zassert_true(match, "String descriptor does not match expected value %s", expected.data());

    LOG_DBG("String descriptor %d: %s", index, expected.data());
}

ZTEST(c2usb_usb_device_virtual, test_get_device_info)
{
    auto* dev = usb::test::host::wait_for_device();

    zassert_not_null(dev, "No USB device enumerated on virtual host");

    auto device_desc = dev->control_in<usb::standard::descriptor::device>(
        {usb::standard::device::GET_DESCRIPTOR,
         uint8_t(usb::standard::descriptor::type::DEVICE) << 8, 0,
         sizeof(usb::standard::descriptor::device)});

    zassert_true(device_desc.has_data(), "Failed to read device descriptor");
    zassert_true(device_desc.exact_size(), "Device descriptor size mismatch");

    zassert_equal(device_desc->idVendor, TEST_VID, "Unexpected device VID");
    zassert_equal(device_desc->idProduct, TEST_PID, "Unexpected device PID");

    LOG_DBG("Device descriptor: VID=0x%04x, PID=0x%04x, bcdDevice=%x.%x",
            uint16_t(device_desc->idVendor), uint16_t(device_desc->idProduct),
            device_desc->bcdDevice >> 8, device_desc->bcdDevice & 0xFF);

    test_device_string(dev, device_desc->iManufacturer, product_info.vendor_name);
    test_device_string(dev, device_desc->iProduct, product_info.product_name);
}

ZTEST(c2usb_usb_device_virtual, test_get_config_info)
{
    auto* dev = usb::test::host::wait_for_device();

    zassert_not_null(dev, "No USB device enumerated on virtual host");

    auto config_desc = dev->control_in<usb::standard::descriptor::configuration>(
        {usb::standard::device::GET_DESCRIPTOR,
         uint8_t(usb::standard::descriptor::type::CONFIGURATION) << 8, 0, 512});

    zassert_true(config_desc.has_data(), "Failed to read configuration descriptor");
    zassert_equal(config_desc->wTotalLength, config_desc.as_span().size(),
                  "Configuration descriptor total length mismatch");

    // TODO: attributes, max power

    size_t desc_count = 0;
    size_t interface_count = 0;
    size_t endpoint_count = 0;
    static constexpr auto endpoints = std::to_array<usb::endpoint::address>(
        {usb::endpoint::address(0x8f), usb::endpoint::address(0x01), usb::endpoint::address(0x81)});

    auto desc_set = usb::descriptor_set(config_desc.as_span());
    for (auto it = desc_set.begin(); it != desc_set.end(); ++it)
    {
        zassert_true(it.valid(), "Invalid descriptor found in configuration descriptor set");

        LOG_HEXDUMP_DBG(it.data(), it.header()->bLength, "Descriptor");

        if (auto* iad_desc = it.as<usb::standard::descriptor::interface_association>())
        {
            // CDC ACM function IAD descriptor
            zassert_equal(interface_count, 0, "Unexpected IAD descriptor found after interfaces");
            zassert_equal(iad_desc->bFirstInterface, 0,
                          "Unexpected IAD descriptor first interface number");
            zassert_equal(iad_desc->bInterfaceCount, 2,
                          "Unexpected IAD descriptor interface count");
            zassert_equal(iad_desc->bFunctionClass, uint8_t(usb::cdc::CLASS_CODE),
                          "Unexpected IAD descriptor function class");
            zassert_equal(iad_desc->bFunctionSubClass,
                          uint8_t(usb::cdc::subclass::ABSTRACT_CONTROL_MODEL),
                          "Unexpected IAD descriptor function subclass");
            zassert_equal(iad_desc->bFunctionProtocol,
                          uint8_t(usb::cdc::protocol_code::ITU_T_Vp250),
                          "Unexpected IAD descriptor function protocol");

#ifdef CONFIG_SHELL_C2USB_FUNCTION_NAME
            if (sizeof(CONFIG_SHELL_C2USB_FUNCTION_NAME) > 1)
            {
                test_device_string(dev, iad_desc->iFunction, CONFIG_SHELL_C2USB_FUNCTION_NAME);
            }
#else
            {
                zassert_equal(iad_desc->iFunction, 0,
                              "Unexpected IAD descriptor function string index");
            }
#endif
        }
        else if (auto* if_desc = it.as<usb::standard::descriptor::interface>())
        {
            if (if_desc->bAlternateSetting == 0)
            {
                zassert_equal(if_desc->bInterfaceNumber, interface_count,
                              "Interface descriptor has unexpected endpoints");
                switch (if_desc->bInterfaceNumber)
                {
                case 0: // CDC ACM comm interface
                    zassert_equal(if_desc->bNumEndpoints, 1,
                                  "CDC ACM comm interface has unexpected endpoints");
                    break;
                case 1: // CDC ACM data interface
                    zassert_equal(if_desc->bNumEndpoints, 2,
                                  "CDC ACM data interface has unexpected endpoints");
                    break;
                case 2: // DFU runtime interface
                    zassert_equal(if_desc->bNumEndpoints, 0,
                                  "DFU runtime interface has unexpected endpoints");

                    test_device_string(dev, if_desc->iInterface, dfu_runtime_fn().name());
                    break;
                default:
                    zassert_true(false, "Unexpected interface number %u",
                                 if_desc->bInterfaceNumber);
                    break;
                }
                interface_count++;
            }
            else
            {
                // this would be different if audio or NCM class was in the configuration
                zassert_true(false, "Unexpected alternate setting %u", if_desc->bAlternateSetting);
            }
        }
        else if (auto* ep_desc = it.as<usb::standard::descriptor::endpoint>())
        {
            zassert_true(endpoint_count < endpoints.size(), "Unexpected endpoint descriptor found");
            zassert_equal(endpoints[endpoint_count],
                          usb::endpoint::address(ep_desc->bEndpointAddress),
                          "Unexpected endpoint address 0x%02x at %d",
                          uint8_t(ep_desc->bEndpointAddress), endpoint_count);
            endpoint_count++;
        }
        else if (auto* dfu_func_desc = it.as<usb::dfu::descriptor::functional>())
        {
            zassert_equal(interface_count, 3,
                          " DFU function descriptor found before DFU interface descriptor");
            zassert_equal(dfu_func_desc->bmAttributes.will_detach, true,
                          "Unexpected DFU function descriptor will_detach attribute");
            zassert_equal(dfu_func_desc->bcdDFUVersion, usb::dfu::SPEC_VERSION,
                          "Unexpected DFU function descriptor version");
        }
        desc_count++;
    }

    zassert_equal(interface_count, 3,
                  "Unexpected number of interfaces found in configuration descriptor");
    zassert_equal(endpoint_count, endpoints.size(),
                  "Unexpected number of endpoints found in configuration descriptor");
}

ZTEST(c2usb_usb_device_virtual, test_dfu_detach_request)
{
    auto* dev = usb::test::host::wait_for_device();

    zassert_not_null(dev, "No USB device enumerated on virtual host");

    constexpr auto detach_request = usb::control::request{usb::dfu::control::DETACH, 0,
                                                          // DFU interface index:
                                                          2};

    auto conf = dev->get_configuration();
    zassert_true(conf.has_value(), "Failed to get device configuration");

    if (conf.value() == 0)
    {
        auto success = dev->control_out(detach_request);
        zassert_false(success,
                      "DFU DETACH request was not rejected when device was not configured");

        success = dev->set_configuration(1);
        zassert_true(success, "Failed to set device configuration");
    }

    auto success = dev->control_out(detach_request);
    zassert_true(success, "DFU DETACH request was rejected when device was configured");
    zassert_equal(dfu_detach_req_count, 1,
                  "DFU DETACH request was not handled when device was configured");
}

static void* test_setup()
{
    int err = usb::test::host::start();
    zassert_equal(err, 0, "Failed to start USB host");

    err = usb::test::host::bus_reset();
    zassert_equal(err, 0, "Failed to issue bus reset");

    err = usb::test::host::bus_resume();
    zassert_equal(err, 0, "Failed to issue bus resume");

    err = usb::test::host::sof_enable();
    zassert_equal(err, 0, "Failed to enable SOF generation");

    auto& dev = loop_device().emplace(mac(), product_info);

    dev.set_config_for_speed(loop_config<usb::speed::FULL>("fs-cfg"), usb::speed::FULL);
    if constexpr (usb::df::zephyr::udc_mac::supported_speeds().includes(usb::speed::HIGH))
    {
        dev.set_config_for_speed(loop_config<usb::speed::HIGH>("hs-cfg"), usb::speed::HIGH);
    }

    dev.set_power_event_delegate(
        [](usb::df::device& dev, usb::df::device::event ev)
        {
            if (ev == usb::df::device::event::CONFIGURATION_CHANGE)
            {
                LOG_DBG("USB configured: %u, granted current: %uuA", (unsigned)dev.configured(),
                        dev.granted_bus_current_uA());
            }
            else
            {
                LOG_DBG("USB power state: %s, granted current: %uuA",
                        magic_enum::enum_name(dev.power_state()).data(),
                        dev.granted_bus_current_uA());
            }
        });
    dev.open();

    this_thread::sleep_for(200ms);

    return nullptr;
}

static void test_teardown(void*)
{
    loop_device()->close();
    loop_device().reset();

    int err = usb::test::host::stop();
    zassert_equal(err, 0, "Failed to disable USB host");
}

ZTEST_SUITE(c2usb_usb_device_virtual, nullptr, test_setup, nullptr, nullptr, test_teardown);
