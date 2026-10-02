// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "usb/control.hpp"
#include "usb/standard/descriptors.hpp"

// https://www.w3.org/
// https://usb.spec.whatwg.org/
namespace usb::webusb
{
constexpr version PROTOCOL_VERSION{"1.0"};
static constexpr uint8_t VENDOR_CODE =
    2; // configurable, don't allow conflicts with other vendor codes

enum struct request_code : uint8_t
{
    GET_URL = 2,
};

enum struct descriptor_type : uint8_t
{
    URL = 3,
};

enum struct url_scheme : uint8_t
{
    HTTP = 0,
    HTTPS = 1,
    NONE = 0xff,
};

struct url_descriptor : public descriptor<url_descriptor>
{
    static constexpr auto TYPE_CODE = descriptor_type::URL;

    url_scheme bScheme{};
    // NOLINTNEXTLINE(*-avoid-c-arrays)
    char8_t URL[0];

    static constexpr size_t url_offset()
    {
        return sizeof(usb::descriptor_header) + sizeof(bScheme);
    }
    constexpr url_descriptor() = default;
    constexpr url_descriptor(uint8_t url_length)
        : usb::descriptor<url_descriptor>(url_offset() + url_length)
    {}
    [[nodiscard]] std::u8string_view url() const
    {
        if (bLength <= url_offset())
        {
            return {};
        }
        return {URL, (bLength - url_offset())};
    }
};

struct protocol_info
{
    le_uint16_t bcdVersion{PROTOCOL_VERSION};
    uint8_t bVendorCode{VENDOR_CODE}; // bRequest value used for issuing WebUSB requests.
    istring iLandingPage{};           // optional URL descriptor index of the device’s landing page.
};

struct platform_descriptor : public standard::descriptor::device_capability::platform<protocol_info>
{
    // 3408b638-09a9-47a0-8bfd-a0768815b665
    constexpr static uuid UUID = {0x38, 0xB6, 0x08, 0x34, 0xA9, 0x09, 0xA0, 0x47,
                                  0x8B, 0xFD, 0xA0, 0x76, 0x88, 0x15, 0xB6, 0x65};

    constexpr platform_descriptor()
        : standard::descriptor::device_capability::platform<protocol_info>()
    {
        PlatformCapabilityUUID = UUID;
    }
};

namespace control
{
// wIndex == request_code
constexpr usb::control::request_id WEBUSB{direction::IN, usb::control::request::type::VENDOR,
                                          usb::control::request::recipient::DEVICE, VENDOR_CODE};

} // namespace control

} // namespace usb::webusb
