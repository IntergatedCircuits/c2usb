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
#ifndef __USB_VENDOR_MICROSOFT_XUSB_HPP_
#define __USB_VENDOR_MICROSOFT_XUSB_HPP_

#include "usb/control.hpp"
#include "usb/standard/descriptors.hpp"

namespace usb::microsoft::xusb
{
// notice the difference in the amount of documentation?
// that's because this protocol isn't published, it's reverse-engineered...
inline namespace xusb22
{
// https://www.partsnotincluded.com/understanding-the-xbox-360-wired-controllers-usb-data/

/* https://github.com/nefarius/ViGEmBus/issues/40#issuecomment-619889180
    XInput devices are exposed as an XInput and a HID device in Windows
    How to separate the triggers in the HID device, and make it report LT/RT on Rx/Ry
    (0x33/0x34 usage) and RStick on Z/Rz (0x32/0x35 usage) HID Axis:
    [HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\xusb22\Parameters]
    "GamepadTriggerUsage"=dword:00003334
    "GamepadStickUsage"=dword:31303532
*/

constexpr const char* COMPATIBLE_ID = "XUSB20";

constexpr uint8_t CLASS_CODE = 0xFF;
constexpr uint8_t SUBCLASS_CODE = 0x5D;
constexpr uint8_t PROTOCOL_CODE = 0x01;

struct control_in
{
    struct joystick
    {
        le_int16_t X{};
        le_int16_t Y{};
    };

    const uint8_t report_id{0};
    const uint8_t report_size{sizeof(control_in)};
    union
    {
        struct
        {
            uint16_t UP : 1;
            uint16_t DOWN : 1;
            uint16_t LEFT : 1;
            uint16_t RIGHT : 1;
            uint16_t START : 1;
            uint16_t BACK : 1;
            uint16_t LS : 1;
            uint16_t RS : 1;
            uint16_t LB : 1;
            uint16_t RB : 1;
            uint16_t HOME : 1;
            uint16_t : 1;
            uint16_t A : 1;
            uint16_t B : 1;
            uint16_t X : 1;
            uint16_t Y : 1;
        };
        le_uint16_t buttons{};
    };
    uint8_t left_trigger{};
    uint8_t right_trigger{};
    joystick left;
    joystick right;
    uint8_t reserved[6]{};
};

struct rumble_out
{
    const uint8_t report_id{0};
    const uint8_t report_size{sizeof(rumble_out)};
    uint8_t reserved1[1]{};
    uint8_t left_rumble{};
    uint8_t right_rumble{};
    uint8_t reserved2[3]{};
};

// https://www.partsnotincluded.com/xbox-360-controller-led-animations-info/
enum class led_animation : uint8_t
{
    OFF = 0x0,
    ALL_BLINKING = 0x1,
    FLASH_1_ON = 0x2,
    FLASH_2_ON = 0x3,
    FLASH_3_ON = 0x4,
    FLASH_4_ON = 0x5,
    ON_1 = 0x6,
    ON_2 = 0x7,
    ON_3 = 0x8,
    ON_4 = 0x9,
    ROTATING_1_2_3_4 = 0xA,
    BLINKING_X = 0xB,
    SLOW_BLINKING_X = 0xC,
    ALTERNATING_RETURN_X = 0xD,
};

struct led_out
{
    const uint8_t report_id{1};
    const uint8_t report_size{sizeof(led_out)};
    led_animation leds{};
};

constexpr size_t MAX_INPUT_REPORT_SIZE = sizeof(control_in);
constexpr size_t MAX_OUTPUT_REPORT_SIZE = std::max(sizeof(rumble_out), sizeof(led_out));

struct descriptor : public usb::descriptor<descriptor>
{
    constexpr static auto TYPE_CODE = 0x21;

    uint8_t bProtocolId[3]{0x00, 0x01, 0x01};
    uint8_t bProductType{0x25};
    endpoint::address bInEpAddress;
    uint8_t bInReportSize{MAX_INPUT_REPORT_SIZE};
    uint8_t bMagicNumbers_p2[5]{0x00, 0x00, 0x00, 0x00, 0x13};
    endpoint::address bOutEpAddress;
    uint8_t bOutReportSize{MAX_OUTPUT_REPORT_SIZE};
    uint8_t MagicNumbers_p3[2]{0x00, 0x00};

    constexpr descriptor(endpoint::address in, endpoint::address out)
        : bInEpAddress(in), bOutEpAddress(out)
    {}
};
} // namespace xusb22
} // namespace usb::microsoft::xusb

#endif // __USB_VENDOR_MICROSOFT_XUSB_HPP_
