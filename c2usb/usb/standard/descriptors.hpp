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
#ifndef __USB_STANDARD_DESCRIPTORS_HPP_
#define __USB_STANDARD_DESCRIPTORS_HPP_

#include "usb/version.hpp"
#include "usb/control.hpp"
#include "usb/endpoint.hpp"
#include <string_view>

namespace usb
{
    namespace standard::descriptor
    {
        struct device;
        struct interface;
        struct interface_association;
    }

    struct class_info
    {
        uint8_t class_code {};
        uint8_t subclass_code {};
        uint8_t protocol_code {};

        constexpr class_info()
        {}
        template <typename TSC, typename TP>
        constexpr class_info(uint8_t class_c, TSC subclass_c, TP protocol_c)
                : class_code(class_c),
                  subclass_code(static_cast<uint8_t>(subclass_c)),
                  protocol_code(static_cast<uint8_t>(protocol_c))
        {}

        friend standard::descriptor::device* operator<<(standard::descriptor::device* desc,
                const class_info& ci);
        friend standard::descriptor::interface* operator<<(standard::descriptor::interface* desc,
                const class_info& ci);
        friend standard::descriptor::interface_association* operator<<(standard::descriptor::interface_association* desc,
                const class_info& ci);
    };
}

namespace usb::standard::descriptor
{
    enum class type : uint8_t
    {
        DEVICE                      = 0x01, /// Device descriptor
        CONFIGURATION               = 0x02, /// Configuration descriptor
        STRING                      = 0x03, /// String descriptor
        INTERFACE                   = 0x04, /// Interface descriptor
        ENDPOINT                    = 0x05, /// Endpoint descriptor
        DEVICE_QUALIFIER            = 0x06, /// Device qualifier descriptor
        OTHER_SPEED_CONFIGURATION   = 0x07, /// Device descriptor for other supported speed
        INTERFACE_POWER             = 0x08, /// Interface power descriptor
        ON_THE_GO                   = 0x09, /// On-the-go (OTG)
        OTG                         = ON_THE_GO,
        DEBUG_                      = 0x0A, ///
        INTERFACE_ASSOCIATION       = 0x0B, /// Interface Association Descriptor
        BINARY_OBJECT_STORE         = 0x0F, /// Binary device Object Store descriptor
        BOS                         = BINARY_OBJECT_STORE,
        DEVICE_CAPABILITY           = 0x10, /// Device capability descriptor (part of BOS)

        SUPERSPEED_USB_ENDPOINT_COMPANION               = 0x30,
        SUPERSPEEDPLUS_ISOCHRONOUS_ENDPOINT_COMPANION   = 0x31,
    };

    struct device : public usb::descriptor<device>
    {
        constexpr static auto TYPE_CODE = standard::descriptor::type::DEVICE;

        version bcdUSB {};             /// USB Specification Number which device complies to
        uint8_t bDeviceClass {};       /// Class Code (Assigned by USB Org)
                                       /// If equal to Zero, each interface specifies its own class code
                                       /// If equal to 0xFF, the class code is vendor specified.
                                       /// Otherwise field is valid Class Code.
        uint8_t bDeviceSubClass {};    /// Subclass Code (Assigned by USB Org)
        uint8_t bDeviceProtocol {};    /// Protocol Code (Assigned by USB Org)
        uint8_t bMaxPacketSize {};     /// Maximum Packet Size for Zero Endpoint. Valid Sizes are 8, 16, 32, 64
        le_uint16_t idVendor {};       /// Vendor ID (Assigned by USB Org)
        le_uint16_t idProduct {};      /// Product ID (Assigned by Manufacturer)
        version bcdDevice {};          /// Device Release Number
        istring iManufacturer {};      /// Index of Manufacturer String Descriptor
        istring iProduct {};           /// Index of Product String Descriptor
        istring iSerialNumber {};      /// Index of Serial Number String Descriptor
        uint8_t bNumConfigurations {}; /// Number of Possible Configurations
    };

    struct configuration : public usb::descriptor<configuration>
    {
        constexpr static auto TYPE_CODE = standard::descriptor::type::CONFIGURATION;

        le_uint16_t wTotalLength {};    /// Total length in bytes of data returned
        uint8_t bNumInterfaces {};      /// Number of Interfaces
        uint8_t bConfigurationValue {}; /// Value to use as an argument to select this configuration
        istring iConfiguration {};      /// Index of String Descriptor describing this configuration
        uint8_t bmAttributes { 0x80 };  /// 0b1[Self Powered][Remote Wakeup]00000
        uint8_t bMaxPower { 100 / 2 };  /// Maximum Power Consumption in 2mA units

        constexpr uint16_t max_power_mA() const
        {
            return bMaxPower * 2;
        }
        constexpr bool self_powered() const
        {
            return static_cast<bool>((bmAttributes >> 6) & 1);
        }
        constexpr bool remote_wakeup() const
        {
            return static_cast<bool>((bmAttributes >> 5) & 1);
        }
    };

    template <size_t SIZE>
    struct language_id : public usb::descriptor<language_id<SIZE>>
    {
        constexpr static auto TYPE_CODE = standard::descriptor::type::STRING;

        std::array<le_uint16_t, SIZE> wLANGID; /// Supported Language Codes (e.g. 0x0409 English - United States)
    };

    struct string : public usb::descriptor<string>
    {
        constexpr static auto TYPE_CODE = standard::descriptor::type::STRING;

        // TODO: https://people.kernel.org/kees/bounded-flexible-arrays-in-c
        le_uint16_t Data[0];

        constexpr string()
        {}
        constexpr string(uint8_t length)
                : usb::descriptor<string>(length)
        {}
        std::enable_if_t<std::endian::native == std::endian::little,
        std::u16string_view> u16string() const
        {
            //return { Data, bLength - offsetof(string, Data) };
            return { reinterpret_cast<const char16_t*>(Data), bLength - sizeof(usb::descriptor_header)};
        }
    };

    struct interface : public usb::descriptor<interface>
    {
        constexpr static auto TYPE_CODE = standard::descriptor::type::INTERFACE;

        uint8_t bInterfaceNumber;      /// Number of Interface
        uint8_t bAlternateSetting;     /// Value used to select alternative setting
        uint8_t bNumEndpoints;         /// Number of Endpoints used for this interface
        uint8_t bInterfaceClass;       /// Class Code (Assigned by USB Org)
        uint8_t bInterfaceSubClass;    /// Subclass Code (Assigned by USB Org)
        uint8_t bInterfaceProtocol;    /// Protocol Code (Assigned by USB Org)
        istring iInterface;            /// Index of String Descriptor Describing this interface
    };

    struct endpoint : public usb::descriptor<endpoint>
    {
        constexpr static auto TYPE_CODE = standard::descriptor::type::ENDPOINT;

        /// Endpoint Address 0b[0=Out / 1=In]000[Endpoint Number]
        usb::endpoint::address bEndpointAddress;

        /// Bits 0..1 Transfer Type
        ///     00 = Control
        ///     01 = Isochronous
        ///     10 = Bulk
        ///     11 = Interrupt
        /// Bits 2..7 are reserved. If Isochronous endpoint,
        /// Bits 3..2 = Synchronisation Type (Iso Mode)
        ///     00 = No Synchonisation
        ///     01 = Asynchronous
        ///     10 = Adaptive
        ///     11 = Synchronous
        /// Bits 5..4 = Usage Type (Iso Mode)
        ///     00 = Data Endpoint
        ///     01 = Feedback Endpoint
        ///     10 = Explicit Feedback Data Endpoint
        ///     11 = Reserved
        uint8_t bmAttributes;

        /// Maximum Packet Size this endpoint is capable of sending or receiving
        le_uint16_t wMaxPacketSize;

        /// Interval for polling endpoint data transfers. Value in frame counts.
        /// Ignored for Bulk & Control Endpoints. Isochronous must equal 1
        /// and field may range from 1 to 255 for interrupt endpoints.
        uint8_t bInterval {};

        constexpr usb::endpoint::address address() const { return bEndpointAddress; }

        constexpr usb::endpoint::type type() const
        {
            return static_cast<usb::endpoint::type>(bmAttributes & 3);
        }
        constexpr usb::endpoint::isochronous::sync synchronization() const
        {
            return static_cast<usb::endpoint::isochronous::sync>((bmAttributes >> 2) & 3);
        }
        constexpr usb::endpoint::isochronous::usage usage() const
        {
            return static_cast<usb::endpoint::isochronous::usage>((bmAttributes >> 4) & 3);
        }
        constexpr uint16_t max_packet_size() const
        {
            return wMaxPacketSize;
        }
        constexpr uint8_t interval() const
        {
            return bInterval;
        }

        constexpr static endpoint bulk(usb::endpoint::address addr, uint16_t mps)
        {
            return endpoint(addr, mps, usb::endpoint::type::BULK);
        }
        constexpr static endpoint bulk(usb::endpoint::address addr, usb::speed speed)
        {
            return endpoint(addr, usb::endpoint::packet_size_limit(usb::endpoint::type::BULK, speed), usb::endpoint::type::BULK);
        }

        constexpr static endpoint interrupt(usb::endpoint::address addr, uint16_t mps, uint8_t interval)
        {
            return endpoint(addr, mps, usb::endpoint::type::INTERRUPT, interval);
        }
        constexpr static endpoint interrupt(usb::endpoint::address addr, usb::speed speed, uint8_t interval)
        {
            return endpoint(addr, usb::endpoint::packet_size_limit(usb::endpoint::type::BULK, speed), usb::endpoint::type::INTERRUPT, interval);
        }

        constexpr static endpoint isochronous(usb::endpoint::address addr, uint16_t mps,
                usb::endpoint::isochronous::sync sync, usb::endpoint::isochronous::usage usage)
        {
            return endpoint(addr, mps, sync, usage);
        }

    private:
        constexpr endpoint(usb::endpoint::address addr, std::uint16_t mps,
                usb::endpoint::type t, uint8_t interval = 0)
                : bEndpointAddress(addr),
                  bmAttributes(static_cast<uint8_t>(t)),
                  wMaxPacketSize(mps),
                  bInterval(interval)
        {}

        constexpr endpoint(usb::endpoint::address addr, std::uint16_t mps,
                usb::endpoint::isochronous::sync sync, usb::endpoint::isochronous::usage usage)
                : bEndpointAddress(addr),
                  bmAttributes(static_cast<uint8_t>(usb::endpoint::type::ISOCHRONOUS) |
                          (static_cast<uint8_t>(sync) << 2) | (static_cast<uint8_t>(usage) << 4)),
                  wMaxPacketSize(mps),
                  bInterval(1)
        {}
    };

    struct device_qualifier : public usb::descriptor<device_qualifier>
    {
        constexpr static auto TYPE_CODE = standard::descriptor::type::DEVICE_QUALIFIER;

        version bcdUSB {};              /// USB Specification Number which device complies to
        uint8_t bDeviceClass {};        /// Class Code (Assigned by USB Org)
                                        /// If equal to Zero, each interface specifies its own class code
                                        /// If equal to 0xFF, the class code is vendor specified.
                                        /// Otherwise field is valid Class Code.
        uint8_t bDeviceSubClass {};     /// Subclass Code (Assigned by USB Org)
        uint8_t bDeviceProtocol {};     /// Protocol Code (Assigned by USB Org)
        uint8_t bMaxPacketSize {};      /// Maximum Packet Size for Zero Endpoint. Valid Sizes are 8, 16, 32, 64
        uint8_t bNumConfigurations {};  /// Number of Possible Configurations
        uint8_t bReserved {};           /// Keep 0
    };

    struct binary_object_store : public usb::descriptor<binary_object_store>
    {
        constexpr static auto TYPE_CODE = standard::descriptor::type::BINARY_OBJECT_STORE;

        le_uint16_t wTotalLength {};    /// Total length in bytes of data returned
        uint8_t bNumDeviceCaps {};      /// Number of device capabilities to follow
    };

    namespace device_capability
    {
        enum class type : uint8_t
        {
            WIRELESS_USB                = 0x01, /// Defines the set of Wireless USB-specific device level capabilities
            USB_2p0_EXTENSION           = 0x02, /// USB 2.0 Extension Descriptor
            SUPERSPEED_USB              = 0x03, /// Defines the set of SuperSpeed USB specific device level capabilities
            CONTAINER_ID                = 0x04, /// Defines the instance unique ID used to identify the instance across all operating modes
            PLATFORM                    = 0x05, /// Defines a device capability specific to a particular platform/operating system
            POWER_DELIVERY_CAPABILITY   = 0x06, /// Defines the various PD Capabilities of this device
            BATTERY_INFO_CAPABILITY     = 0x07, /// Provides information on each battery supported by the device
            PD_CONSUMER_PORT_CAPABILITY = 0x08, /// The consumer characteristics of a port on the device
            PD_PROVIDER_PORT_CAPABILITY = 0x09, /// The provider characteristics of a port on the device
            SUPERSPEED_PLUS             = 0x0A, /// Defines the set of SuperSpeed Plus USB specific device level capabilities
            PRECISION_TIME_MEASUREMENT  = 0x0B, /// Precision Time Measurement (PTM) Capability Descriptor
            WIRELESS_USB_EXT            = 0x0C, /// Defines the set of Wireless USB 1.1-specific device level capabilities
            BILLBOARD                   = 0x0D, /// Billboard capability
            AUTHENTICATION              = 0x0E, /// Authentication Capability Descriptor
            BILLBOARD_EX                = 0x0F, /// Billboard Ex capability
            CONFIGURATION_SUMMARY       = 0x10, /// Summarizes configuration information for a function implemented by the device
            FW_STATUS_CAPABILITY        = 0x11, /// Describes the capability to support FWStatus
        };

        template <class T>
        struct descriptor : public usb::descriptor<T>
        {
            constexpr static auto TYPE_CODE = standard::descriptor::type::DEVICE_CAPABILITY;

            constexpr static device_capability::type capability_type()
            {
                return static_cast<device_capability::type>(T::CAP_TYPE_CODE);
            }

            uint8_t bDevCapabilityType { static_cast<uint8_t>(capability_type()) };
        };

        struct usb_2p0_extension : public device_capability::descriptor<usb_2p0_extension>
        {
            constexpr static auto CAP_TYPE_CODE = device_capability::type::USB_2p0_EXTENSION;

            le_uint32_t bmAttributes {};

            union attributes
            {
                uint32_t w {};
                struct
                {
                    uint32_t : 1;
                    bool link_power_mgmt : 1;           /// Link Power Management support
                    bool besl_alt_hird : 1;             /// BESL and alternate HIRD definitions support
                    bool baseline_besl_valid : 1;       /// Recommended Baseline BESL valid
                    bool deep_besl_valid : 1;           /// Recommended Deep BESL valid
                    uint32_t : 3;
                    uint32_t baseline_besl_value : 4;   /// Recommended Baseline BESL value
                    uint32_t deep_besl_value : 4;       /// Recommended Deep BESL value
                    uint32_t : 16;
                };
                constexpr operator uint32_t() const { return w; }
                constexpr operator uint32_t&() { return w; };
            };

            constexpr usb_2p0_extension() = default;
            constexpr usb_2p0_extension(attributes attr)
                    : device_capability::descriptor<usb_2p0_extension>(), bmAttributes(attr)
            {}
        };

        template <class T>
        struct platform : public device_capability::descriptor<platform<T>>
        {
            constexpr static auto CAP_TYPE_CODE = device_capability::type::PLATFORM;

            uint8_t bReserved {};
            uuid PlatformCapabilityUUID {};
            T CapabilityData {};
        };

        template <size_t CONFIGS_SIZE>
        struct configuration_summary : public device_capability::descriptor<configuration_summary<CONFIGS_SIZE>>
        {
            constexpr static auto CAP_TYPE_CODE = device_capability::type::CONFIGURATION_SUMMARY;

            const version bcdVersion { 1, 0 };
            uint8_t bClass {};
            uint8_t bSubClass {};
            uint8_t bProtocol {};
            const uint8_t bConfigurationCount { CONFIGS_SIZE };
            std::array<uint8_t, CONFIGS_SIZE> bConfigurationIndex {};
        };
    }

    struct interface_association : public usb::descriptor<interface_association>
    {
        constexpr static auto TYPE_CODE = standard::descriptor::type::INTERFACE_ASSOCIATION;

        uint8_t bFirstInterface {};     /// First associated interface
        uint8_t bInterfaceCount {};     /// Number of contiguous associated interfaces
        uint8_t bFunctionClass {};      /// Class Code (Assigned by USB Org)
        uint8_t bFunctionSubClass {};   /// Subclass Code (Assigned by USB Org)
        uint8_t bFunctionProtocol {};   /// Protocol Code (Assigned by USB Org)
        istring iFunction {};           /// Index of String Descriptor Describing this function

        constexpr static usb::class_info default_codes()
        {
            return { 0xEF,  /// Miscellaneous Device Class
                0x02,       /// Common Class
                0x01        /// Interface Association Descriptor
            };
        }
    };

    struct otg : public usb::descriptor<otg>
    {
        constexpr static auto TYPE_CODE = standard::descriptor::type::OTG;

        uint8_t bmAttributes {};        /// D7...4: Reserved (reset to zero)
                                        /// D3: RSP support
                                        /// D2: ADP support
                                        /// D1: HNP support
                                        /// D0: SRP support
        version bcdOTG {};              /// The release of the OTG and EH supplement
    };
}

#endif // __USB_STANDARD_DESCRIPTORS_HPP_
