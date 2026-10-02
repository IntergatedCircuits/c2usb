#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/ztest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <ranges>
#include <string_view>

#include <hid/example/simple_keyboard.hpp>
#include <usb/df/class/cdc_acm.hpp>
#include <usb/df/class/dfu.hpp>
#include <usb/df/class/hid.hpp>
#include <usb/df/config_factory.hpp>
#include <usb/df/device.hpp>
#include <usb/df/vendor/microsoft/os_extension.hpp>
#include <usb/df/vendor/webusb.hpp>
#include <usb/standard/descriptor_set.hpp>
#if !CONFIG_C2USB_MCUMGR_SMP_CONSOLE
#include <usb/df/vendor/zephyr/shell.hpp>
#else
#include <usb/df/vendor/zephyr/smp_console.hpp>
#endif
#include <usb/df/vendor/zephyr/udc_mac.hpp>
#include <usb/product_info.hpp>
#include <usb/standard/descriptors.hpp>
#include <usb/standard/requests.hpp>
#include <usb_host.hpp>
#include <zephyr/thread.hpp>

LOG_MODULE_REGISTER(c2usb_usb_device_virtual, LOG_LEVEL_DBG);

using namespace zephyr;
using namespace std::chrono_literals;

constexpr uint16_t TEST_VID = 0x2fe3;
constexpr uint16_t TEST_PID = 0x1201;
constexpr uint16_t TEST_LANG_ID = 0x0409;
static constexpr char8_t WEBUSB_URL[]{u8"example.com/device"};
static constexpr char WEBUSB_LANDING_PAGE[] = "https://example.com/landing_page";

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

constexpr uint8_t DFU_INTERFACE_INDEX = 2;
constexpr size_t USB_INTERFACE_COUNT = 3;

template <usb::speed SPEED>
const auto& loop_config(const char* name)
{
    const auto config_header = usb::df::config::header(usb::df::config::power::bus(100), name);

    static const auto cfg = usb::df::config::make_config(
        config_header,
#if CONFIG_C2USB_MCUMGR_SMP_CONSOLE
        usb::df::zephyr::smp_console::handle()
#else
        usb::df::zephyr::shell::handle()
#endif
            .config_entry(SPEED, usb::endpoint::address(0x01), usb::endpoint::address(0x81),
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

auto& device_extensions()
{
    static usb::df::webusb webusb_ext{WEBUSB_URL, usb::webusb::url_scheme::HTTPS,
                                      WEBUSB_LANDING_PAGE};
    static constinit usb::df::microsoft::descriptors ms_ext;
    static constexpr auto extension_array =
        c2usb::make_reference_array<usb::df::vendor::extension>(webusb_ext, ms_ext);
    static usb::df::vendor::extension_set extension_set{extension_array};
    return extension_set;
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

struct bos_extension_info
{
    usb::istring webusb_landing_page{};
    uint16_t msos_descriptor_set_length{};
};

static bos_extension_info test_bos_extensions(usb::test::device* dev)
{
    auto bos_header = dev->control_in<usb::standard::descriptor::binary_object_store>(
        {usb::standard::device::GET_DESCRIPTOR, uint8_t(usb::standard::descriptor::type::BOS) << 8,
         0, sizeof(usb::standard::descriptor::binary_object_store)});

    zassert_true(bos_header.has_data(), "Failed to read BOS descriptor header");
    zassert_true(bos_header.exact_size(), "BOS descriptor header size mismatch");
    zassert_true(bos_header->wTotalLength >= sizeof(*bos_header),
                 "BOS total length is smaller than its header");

    auto bos_desc = dev->control_in<usb::standard::descriptor::binary_object_store>(
        {usb::standard::device::GET_DESCRIPTOR, uint8_t(usb::standard::descriptor::type::BOS) << 8,
         0, uint16_t(bos_header->wTotalLength + 1)});

    zassert_true(bos_desc.has_data(), "Failed to read complete BOS descriptor");
    zassert_equal(bos_desc->wTotalLength, bos_desc.as_span().size(),
                  "BOS total length does not match returned data");

    usb::standard::bos_capability_set capabilities{bos_desc.as_span()};
    zassert_true(capabilities.valid(), "BOS capability set is malformed");
    zassert_equal(capabilities.size(), bos_desc->bNumDeviceCaps,
                  "BOS capability iterator count mismatch");
    zassert_equal(capabilities.size(), 3,
                  "BOS must contain USB 2.0, WebUSB, and Microsoft OS capabilities");

    bos_extension_info info{};
    bool found_webusb = false;
    bool found_msos = false;
    bool found_usb2_extension = false;

    for (auto it = capabilities.begin(); it != capabilities.end(); ++it)
    {
        zassert_true(it.valid(), "Invalid BOS capability descriptor");

        if (auto* usb2_cap =
                it.as<usb::standard::descriptor::device_capability::usb_2p0_extension>();
            usb2_cap != nullptr and
            usb2_cap->bDevCapabilityType ==
                uint8_t(usb::standard::descriptor::device_capability::type::USB_2p0_EXTENSION))
        {
            zassert_equal(usb2_cap->bLength,
                          sizeof(usb::standard::descriptor::device_capability::usb_2p0_extension),
                          "Unexpected USB 2.0 Extension capability size");
            found_usb2_extension = true;
        }
        else if (auto* webusb_cap = it.as<usb::webusb::platform_descriptor>();
                 webusb_cap != nullptr and
                 webusb_cap->PlatformCapabilityUUID == usb::webusb::platform_descriptor::UUID)
        {
            zassert_equal(webusb_cap->bDevCapabilityType,
                          uint8_t(usb::standard::descriptor::device_capability::type::PLATFORM),
                          "Unexpected WebUSB capability type");
            zassert_equal(webusb_cap->bLength, sizeof(*webusb_cap),
                          "Unexpected WebUSB capability size");
            zassert_equal(uint16_t(webusb_cap->CapabilityData.bcdVersion),
                          uint16_t(usb::webusb::PROTOCOL_VERSION), "Unexpected WebUSB version");
            zassert_equal(webusb_cap->CapabilityData.bVendorCode, usb::webusb::VENDOR_CODE,
                          "Unexpected WebUSB vendor request code");
            zassert_not_equal(webusb_cap->CapabilityData.iLandingPage, 0,
                              "WebUSB landing page string index must not be zero");
            info.webusb_landing_page = webusb_cap->CapabilityData.iLandingPage;
            found_webusb = true;
        }
        else if (auto* msos_cap = it.as<usb::microsoft::platform_descriptor>();
                 msos_cap != nullptr and
                 msos_cap->PlatformCapabilityUUID == usb::microsoft::platform_descriptor::UUID)
        {
            zassert_equal(msos_cap->bDevCapabilityType,
                          uint8_t(usb::standard::descriptor::device_capability::type::PLATFORM),
                          "Unexpected Microsoft OS capability type");
            zassert_equal(msos_cap->bLength, sizeof(*msos_cap),
                          "Unexpected Microsoft OS capability size");
            zassert_equal(msos_cap->CapabilityData.dwWindowsVersion,
                          usb::microsoft::MIN_WINDOWS_VERSION,
                          "Unexpected MS OS capability Windows version");
            zassert_equal(msos_cap->CapabilityData.bMS_VendorCode, usb::microsoft::VENDOR_CODE,
                          "Unexpected MS OS capability vendor request code");
            zassert_equal(msos_cap->CapabilityData.bAltEnumCode, 0,
                          "Unexpected MS OS alternate enumeration code");
            info.msos_descriptor_set_length =
                msos_cap->CapabilityData.wMSOSDescriptorSetTotalLength;
            zassert_true(info.msos_descriptor_set_length >= sizeof(usb::microsoft::set_header),
                         "MS OS capability advertises an invalid descriptor set length");
            found_msos = true;
        }
        else
        {
            zassert_true(false, "Unexpected BOS capability descriptor");
        }
    }

    zassert_true(found_webusb, "WebUSB platform capability was not found");
    zassert_true(found_msos, "Microsoft OS platform capability was not found");
    zassert_true(found_usb2_extension, "Required USB 2.0 Extension capability was not found");
    return info;
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

ZTEST(c2usb_usb_device_virtual, test_get_webusb_descriptors)
{
    auto* dev = usb::test::host::wait_for_device();

    zassert_not_null(dev, "No USB device enumerated on virtual host");
    auto bos_info = test_bos_extensions(dev);
    test_device_string(dev, bos_info.webusb_landing_page, WEBUSB_LANDING_PAGE);

    constexpr usb::control::request get_url{usb::webusb::control::WEBUSB, 1,
                                            uint16_t(usb::webusb::request_code::GET_URL), 255};
    auto url_desc = dev->control_in<usb::webusb::url_descriptor>(get_url);

    zassert_true(url_desc.has_data(), "Failed to read WebUSB URL descriptor");
    zassert_equal(url_desc->bDescriptorType, uint8_t(usb::webusb::descriptor_type::URL),
                  "WebUSB URL descriptor has invalid type");
    zassert_equal(url_desc->bLength, url_desc.as_span().size(),
                  "WebUSB URL descriptor length mismatch: descriptor=%u transfer=%u",
                  url_desc->bLength, unsigned(url_desc.as_span().size()));
    zassert_equal(url_desc->bScheme, usb::webusb::url_scheme::HTTPS,
                  "Unexpected WebUSB URL scheme");
    zassert_true(url_desc->url() == WEBUSB_URL, "WebUSB URL does not match configured URL");
}

ZTEST(c2usb_usb_device_virtual, test_get_msos2_descriptors)
{
    auto* dev = usb::test::host::wait_for_device();

    zassert_not_null(dev, "No USB device enumerated on virtual host");

    auto bos_info = test_bos_extensions(dev);

    const usb::control::request get_msos2_descriptor{
        usb::microsoft::control::GET_DESCRIPTOR, 0,
        uint16_t(usb::microsoft::descriptor_type::MS_OS_20_SET_HEADER_DESCRIPTOR),
        bos_info.msos_descriptor_set_length};
    auto msos_desc = dev->control_in<usb::microsoft::set_header>(get_msos2_descriptor);

    zassert_true(msos_desc.has_data(), "Failed to read Microsoft OS 2.0 descriptor set");
    const auto msos_bytes = msos_desc.as_span();
    usb::microsoft::set_header set_header{};
    std::memcpy(&set_header, msos_bytes.data(), sizeof(set_header));
    zassert_equal(set_header.wLength, sizeof(set_header), "Invalid MS OS 2.0 set header length");
    zassert_equal(set_header.wDescriptorType,
                  uint16_t(usb::microsoft::descriptor_type::MS_OS_20_SET_HEADER_DESCRIPTOR),
                  "Invalid MS OS 2.0 set header type");
    zassert_equal(set_header.dwWindowsVersion, usb::microsoft::MIN_WINDOWS_VERSION,
                  "Unexpected MS OS 2.0 minimum Windows version");
    zassert_equal(set_header.wTotalLength, msos_bytes.size(),
                  "MS OS 2.0 set total length does not match returned data");
    zassert_equal(set_header.wTotalLength, bos_info.msos_descriptor_set_length,
                  "MS OS 2.0 set length does not match BOS capability");

    constexpr size_t config_offset = sizeof(usb::microsoft::set_header);
    zassert_true(msos_bytes.size() >= config_offset + sizeof(usb::microsoft::config_subset_header),
                 "Missing MS OS 2.0 configuration subset");
    usb::microsoft::config_subset_header config_header{};
    std::memcpy(&config_header, msos_bytes.data() + config_offset, sizeof(config_header));
    zassert_equal(config_header.wLength, sizeof(config_header),
                  "Invalid MS OS 2.0 configuration subset header length");
    zassert_equal(config_header.wDescriptorType,
                  uint16_t(usb::microsoft::descriptor_type::MS_OS_20_SUBSET_HEADER_CONFIGURATION),
                  "Invalid MS OS 2.0 configuration subset header type");
    zassert_equal(config_header.bConfigurationValue, 0, "Unexpected MS OS 2.0 configuration index");
    zassert_equal(config_header.bReserved, 0, "MS OS 2.0 configuration reserved field is not zero");

    constexpr size_t function_offset = config_offset + sizeof(config_header);
    zassert_true(msos_bytes.size() >=
                     function_offset + sizeof(usb::microsoft::function_subset_header),
                 "Missing MS OS 2.0 function subset");
    usb::microsoft::function_subset_header function_header{};
    std::memcpy(&function_header, msos_bytes.data() + function_offset, sizeof(function_header));
    zassert_equal(function_header.wLength, sizeof(function_header),
                  "Invalid MS OS 2.0 function subset header length");
    zassert_equal(function_header.wDescriptorType,
                  uint16_t(usb::microsoft::descriptor_type::MS_OS_20_SUBSET_HEADER_FUNCTION),
                  "Invalid MS OS 2.0 function subset header type");
    zassert_equal(function_header.bFirstInterface, DFU_INTERFACE_INDEX,
                  "WINUSB function subset does not identify the DFU interface");
    zassert_equal(function_header.bReserved, 0, "MS OS 2.0 function reserved field is not zero");

    constexpr size_t compatible_id_offset = function_offset + sizeof(function_header);
    zassert_true(msos_bytes.size() >= compatible_id_offset + sizeof(usb::microsoft::compatible_id),
                 "Missing MS OS 2.0 compatible ID descriptor");
    usb::microsoft::compatible_id compatible_id{};
    std::memcpy(&compatible_id, msos_bytes.data() + compatible_id_offset, sizeof(compatible_id));
    zassert_equal(compatible_id.wLength, sizeof(compatible_id),
                  "Invalid MS OS 2.0 compatible ID descriptor length");
    zassert_equal(compatible_id.wDescriptorType,
                  uint16_t(usb::microsoft::descriptor_type::MS_OS_20_FEATURE_COMPATBLE_ID),
                  "Invalid MS OS 2.0 compatible ID descriptor type");
    constexpr std::array<char, 8> WINUSB_ID{'W', 'I', 'N', 'U', 'S', 'B', 0, 0};
    constexpr std::array<char, 8> EMPTY_SUBCOMPATIBLE_ID{};
    zassert_true(compatible_id.CompatibleID == WINUSB_ID,
                 "DFU compatible ID does not match WINUSB");
    zassert_true(compatible_id.SubCompatibleID == EMPTY_SUBCOMPATIBLE_ID,
                 "Unexpected DFU sub-compatible ID");

    constexpr size_t expected_config_length =
        sizeof(config_header) + sizeof(function_header) + sizeof(compatible_id);
    zassert_equal(config_header.wTotalLength, expected_config_length,
                  "MS OS 2.0 configuration subset length mismatch");
    zassert_equal(function_header.wSubsetLength, sizeof(function_header) + sizeof(compatible_id),
                  "MS OS 2.0 function subset length mismatch");
    zassert_equal(compatible_id_offset + sizeof(compatible_id), msos_bytes.size(),
                  "MS OS 2.0 descriptor set has trailing or unaccounted bytes");
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

    auto desc_set = usb::standard::descriptor_set(config_desc.as_span());
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

#if CONFIG_C2USB_MCUMGR_SMP_CONSOLE
            if (sizeof(CONFIG_C2USB_MCUMGR_SMP_CONSOLE_FUNCTION_NAME) > 1)
            {
                test_device_string(dev, iad_desc->iFunction,
                                   CONFIG_C2USB_MCUMGR_SMP_CONSOLE_FUNCTION_NAME);
            }
            else
            {
                zassert_equal(iad_desc->iFunction, 0,
                              "Unexpected IAD descriptor function string index");
            }
#else
            if (sizeof(CONFIG_SHELL_C2USB_FUNCTION_NAME) > 1)
            {
                test_device_string(dev, iad_desc->iFunction, CONFIG_SHELL_C2USB_FUNCTION_NAME);
            }
            else
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
                case DFU_INTERFACE_INDEX:
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
            zassert_equal(interface_count, USB_INTERFACE_COUNT,
                          " DFU function descriptor found before DFU interface descriptor");
            zassert_equal(dfu_func_desc->bmAttributes.will_detach, true,
                          "Unexpected DFU function descriptor will_detach attribute");
            zassert_equal(dfu_func_desc->bcdDFUVersion, usb::dfu::SPEC_VERSION,
                          "Unexpected DFU function descriptor version");
        }
        desc_count++;
    }

    zassert_equal(interface_count, USB_INTERFACE_COUNT,
                  "Unexpected number of interfaces found in configuration descriptor");
    zassert_equal(endpoint_count, endpoints.size(),
                  "Unexpected number of endpoints found in configuration descriptor");
}

ZTEST(c2usb_usb_device_virtual, test_dfu_detach_request)
{
    auto* dev = usb::test::host::wait_for_device();

    zassert_not_null(dev, "No USB device enumerated on virtual host");

    constexpr auto detach_request =
        usb::control::request{usb::dfu::control::DETACH, 0, DFU_INTERFACE_INDEX};

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

    auto& dev = loop_device().emplace(mac(), product_info, device_extensions());

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

    err = usb::test::host::bus_resume();
    zassert_equal(err, 0, "Failed to issue bus resume");

    err = usb::test::host::bus_reset();
    zassert_equal(err, 0, "Failed to issue bus reset");

    err = usb::test::host::sof_enable();
    zassert_equal(err, 0, "Failed to enable SOF generation");

    this_thread::sleep_for(10ms);

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
