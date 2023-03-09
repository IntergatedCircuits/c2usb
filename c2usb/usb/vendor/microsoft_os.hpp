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
#ifndef __USB_VENDOR_MICROSOFT_OS_HPP_
#define __USB_VENDOR_MICROSOFT_OS_HPP_

#include "usb/control.hpp"
#include "usb/standard/descriptors.hpp"
#include <string_view>

namespace usb::microsoft
{
    namespace msos_1p0
    {
        static constexpr usb::istring STRING_INDEX = 0xEE;
    }

    inline namespace msos_2p0
    {
        static constexpr uint32_t MIN_WINDOWS_VERSION = 0x06030000;
        static constexpr uint8_t  VENDOR_CODE = 1; // configurable

        namespace control
        {
            // also, wIndex == 7
            constexpr usb::control::request_id GET_DESCRIPTOR {
                direction::IN, usb::control::request::type::VENDOR, usb::control::request::recipient::DEVICE,
                VENDOR_CODE };

            // also, wIndex == 8
            constexpr usb::control::request_id SET_ALT_ENUM {
                direction::OUT, usb::control::request::type::VENDOR, usb::control::request::recipient::DEVICE,
                VENDOR_CODE };
        }

        /// @brief  Descriptor Set Information for Windows 8.1 or later
        struct descriptor_set_info
        {
            /// Minimum Windows version
            le_uint32_t dwWindowsVersion { MIN_WINDOWS_VERSION };

            /// The length, in bytes of the MS OS 2.0 descriptor set
            le_uint16_t wMSOSDescriptorSetTotalLength {};

            /// Vendor defined code to use to retrieve this version of the MS OS 2.0 descriptor
            /// and also to set alternate enumeration behavior on the device
            uint8_t bMS_VendorCode { VENDOR_CODE };

            /// A non-zero value to send to the device to indicate
            /// that the device may return non-default USB descriptors for enumeration.
            /// If the device does not support alternate enumeration, this value shall be 0.
            uint8_t bAltEnumCode {};
        };

        struct platform_descriptor : public standard::descriptor::device_capability::platform<descriptor_set_info>
        {
            constexpr static uuid UUID = {
            0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C,
            0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F };

            constexpr platform_descriptor()
                    : standard::descriptor::device_capability::platform<descriptor_set_info>()
            {
                PlatformCapabilityUUID = UUID;
            }
        };

        enum class descriptor_type : uint8_t
        {
            MS_OS_20_SET_HEADER_DESCRIPTOR       = 0x00,
            MS_OS_20_SUBSET_HEADER_CONFIGURATION = 0x01,
            MS_OS_20_SUBSET_HEADER_FUNCTION      = 0x02,
            MS_OS_20_FEATURE_COMPATBLE_ID        = 0x03,
            MS_OS_20_FEATURE_REG_PROPERTY        = 0x04,
            MS_OS_20_FEATURE_MIN_RESUME_TIME     = 0x05,
            MS_OS_20_FEATURE_MODEL_ID            = 0x06,
            MS_OS_20_FEATURE_CCGP_DEVICE         = 0x07,
            MS_OS_20_FEATURE_VENDOR_REVISION     = 0x08,
        };

        enum class property_data_type : uint8_t
        {
            REG_SZ                  = 0x01, /// A NULL-terminated Unicode String
            REG_EXPAND_SZ           = 0x02, /// A NULL-terminated Unicode String that includes environment variables
            REG_BINARY              = 0x03, /// Free-form binary
            REG_DWORD_LITTLE_ENDIAN = 0x04, /// A little-endian 32-bit integer
            REG_DWORD_BIG_ENDIAN    = 0x05, /// A big-endian 32-bit integer
            REG_LINK                = 0x06, /// A NULL-terminated Unicode string that contains a symbolic link
            REG_MULTI_SZ            = 0x07, /// Multiple NULL-terminated Unicode strings
        };

        template <class T>
        struct descriptor
        {
            constexpr static uint16_t size()
            {
                return sizeof(T);
            }
            constexpr static uint8_t type()
            {
                return static_cast<uint8_t>(T::TYPE_CODE);
            }
            le_uint16_t wLength { size() };
            le_uint16_t wDescriptorType { type() };
        };

        /// @brief  Microsoft OS 2.0 descriptor set header structure
        struct set_header : public descriptor<set_header>
        {
            constexpr static auto TYPE_CODE = descriptor_type::MS_OS_20_SET_HEADER_DESCRIPTOR;

            /// Minimum Windows version
            le_uint32_t dwWindowsVersion { MIN_WINDOWS_VERSION };

            /// The size of entire MS OS 2.0 descriptor set.
            /// The value shall match the value in the descriptor set information structure
            le_uint16_t wTotalLength {};
        };

        /// @brief  Microsoft OS 2.0 configuration subset header structure
        struct config_subset_header : public descriptor<config_subset_header>
        {
            constexpr static auto TYPE_CODE = descriptor_type::MS_OS_20_SUBSET_HEADER_CONFIGURATION;

            /// The configuration value for the USB configuration to which this subset applies
            /// @note: In its current status, this field is actually an index of valid configuration,
            /// so it must be set to bConfigurationValue - 1
            /// https://social.msdn.microsoft.com/Forums/ie/en-US/ae64282c-3bc3-49af-8391-4d174479d9e7/microsoft-os-20-descriptors-not-working-on-an-interface-of-a-composite-usb-device?forum=wdk
            uint8_t bConfigurationValue {};

            uint8_t bReserved {};

            /// The size of entire configuration subset including this header
            le_uint16_t wTotalLength {};
        };

        /// @brief  Microsoft OS 2.0 function subset header structure
        struct function_subset_header : public descriptor<function_subset_header>
        {
            constexpr static auto TYPE_CODE = descriptor_type::MS_OS_20_SUBSET_HEADER_FUNCTION;

            /// The interface number for the first interface of the function to which this subset applies
            uint8_t bFirstInterface {};

            uint8_t bReserved {};

            /// The size of entire function subset including this header
            le_uint16_t wSubsetLength {};
        };

        /// @brief  Microsoft OS 2.0 compatible ID descriptor structure
        struct compatible_id : public descriptor<compatible_id>
        {
            constexpr static auto TYPE_CODE = descriptor_type::MS_OS_20_FEATURE_COMPATBLE_ID;

            /// Compatible ID String
            char CompatibleID[8] {};

            /// Sub-compatible ID String
            char SubCompatibleID[8] {};
        };

        /// @brief  Microsoft OS 2.0 registry property descriptor structure
        template <const char16_t* PROP_NAME, class T>
        struct registry_property : public descriptor<registry_property<PROP_NAME, T>>
        {
            constexpr static auto TYPE_CODE = descriptor_type::MS_OS_20_FEATURE_REG_PROPERTY;
            constexpr static size_t NAME_SIZE = std::wstring_view(PROP_NAME).length() + 1;

            /// The type of registry property (@ref property_data_type)
            le_uint16_t wPropertyDataType {};

            /// The length of the property name
            le_uint16_t wPropertyNameLength {};

            /// The null-terminated? Unicode? name of registry property (variable size)
            char16_t PropertyName[NAME_SIZE] {};

            /// The length of the property data
            le_uint16_t wPropertyDataLength { sizeof(T) };

            /// The data of registry property (variable size)
            T PropertyData {};
        };

        /// @brief  Microsoft OS 2.0 minimum USB recovery time descriptor structure
        /// @details The Microsoft OS 2.0 minimum USB resume time descriptor is used to indicate
        /// to the Windows USB driver stack the minimum time needed to recover after resuming from suspend,
        /// and how long the device requires resume signaling to be asserted.
        /// This descriptor is used for a device operating at high, full, or low-speed.
        /// It is not used for a device operating at SuperSpeed or higher.
        ///
        /// This descriptor allows devices to recover faster than the default 10 millisecond specified
        /// in the USB 2.0 specification. It can also allow the host to assert resume signaling
        /// for less than the 20 milliseconds required in the USB 2.0 specification,
        /// in cases where the timing of resume signaling is controlled by software.
        /// @note  The USB resume time descriptor is applied to the entire device.
        struct minimum_resume_time : public descriptor<minimum_resume_time>
        {
            constexpr static auto TYPE_CODE = descriptor_type::MS_OS_20_FEATURE_MIN_RESUME_TIME;

            /// The number of milliseconds the device requires to recover from port resume [0..10]
            uint8_t bResumeRecoveryTime;

            /// The number of milliseconds the device requires resume signaling to be asserted [1..20]
            uint8_t bResumeSignalingTime;
        };

        /// @brief  Microsoft OS 2.0 model ID descriptor structure
        /// @details The Microsoft OS 2.0 model ID descriptor is used to uniquely identify the physical device.
        /// @note  The model ID descriptor is applied to the entire device.
        struct model_id : public descriptor<model_id>
        {
            constexpr static auto TYPE_CODE = descriptor_type::MS_OS_20_FEATURE_MODEL_ID;

            /// This is a 128-bit number that uniquely identifies a physical device.
            uuid ModelID {};
        };

        /// @brief  Microsoft OS 2.0 CCGP device descriptor structure
        /// @details The Microsoft OS 2.0 CCGP device descriptor is used to indicate
        /// that the device should be treated as a composite device by Windows
        /// regardless of the number of interfaces, configuration, or class, subclass,
        /// and protocol codes, the device reports.
        /// @note  The CCGP device descriptor must be applied to the entire device.
        struct composite_driver : public descriptor<composite_driver>
        {
            constexpr static auto TYPE_CODE = descriptor_type::MS_OS_20_FEATURE_CCGP_DEVICE;
        };

        /// @brief  Microsoft OS 2.0 vendor revision descriptor structure
        /// @details The Microsoft OS 2.0 vendor revision descriptor is used to indicate
        /// the revision of registry property and other MSOS descriptors.
        /// If this value changes between enumerations the registry property descriptors
        /// will be updated in registry during that enumeration. You must always change
        /// this value if you are adding/modifying any registry property or other MSOS descriptors.
        /// @note  The vendor revision descriptor must be applied at the device scope
        /// for a non-composite device or for MSOS descriptors that apply to the device scope
        /// of a composite device.
        /// Additionally, for a composite device, the vendor revision descriptor must be provided
        /// in every function subset and may be updated independently per-function.
        struct vendor_revision : public descriptor<vendor_revision>
        {
            constexpr static auto TYPE_CODE = descriptor_type::MS_OS_20_FEATURE_VENDOR_REVISION;

            /// Revision number associated with the descriptor set.
            /// Modify it every time you add/modify a registry property
            /// or other MSOS descriptor. Set to greater than or equal to 1.
            le_uint16_t wVendorRevision { 1 };
        };
    }
}

#endif // __USB_VENDOR_MICROSOFT_OS_HPP_
