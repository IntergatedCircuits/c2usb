// SPDX-License-Identifier: MPL-2.0
#pragma once

#include "usb/control.hpp"
#include "usb/version.hpp"
#include <bitfilled.hpp>

namespace usb::cdc
{
constexpr version SPEC_VERSION{"1.10"};

inline namespace comm
{
constexpr uint8_t CLASS_CODE = 0x02;

enum class subclass : uint8_t
{
    DIRECT_LINE_CONTROL_MODEL = 0x01,
    ABSTRACT_CONTROL_MODEL = 0x02,
    TELEPHONE_CONTROL_MODEL = 0x03,
    MULTI_CHANNEL_CONTROL_MODEL = 0x04,
    CAPI_CONTROL_MODEL = 0x05,
    ETHERNET_NETWORKING_CONTROL_MODEL = 0x06,
    ATM_NETWORKING_CONTROL_MODEL = 0x07,
    WIRELESS_HANDSET_CONTROL_MODEL = 0x08,
    DEVICE_MANAGEMENT = 0x09,
    MOBILE_DIRECT_LINE_MODEL = 0x0A,
    OBEX = 0x0B,
    ETHERNET_EMULATION_MODEL = 0x0C,
    NETWORK_CONTROL_MODEL = 0x0D,
};

enum class protocol_code : uint8_t
{
    USB = 0,
    ITU_T_Vp250 = 1,
    PCCA_101 = 2,
    PCCA_101_ANNEX_O = 3,
    GSM_7p07 = 4,
    _3GPP_27p07 = 5,
    C_S0017_0 = 6,
    USB_EEM = 7,
};

enum class request : uint8_t
{
    SEND_ENCAPSULATED_COMMAND = 0x00, /// Host command in the supported control protocol format
    GET_ENCAPSULATED_RESPONSE = 0x01, /// Device response in the supported control protocol format
    SET_COMM_FEATURE = 0x02,          /// Change a feature (indexed by wIndex)
    GET_COMM_FEATURE = 0x03,          /// Read a feature (indexed by wIndex)
    CLEAR_COMM_FEATURE = 0x04,        /// Reset a feature (indexed by wIndex) to default state

    SET_AUX_LINE_STATE = 0x10,
    SET_HOOK_STATE = 0x11,
    PULSE_SETUP = 0x12,
    SEND_PULSE = 0x13,
    SET_PULSE_TIME = 0x14,
    RING_AUX_JACK = 0x15,

    SET_LINE_CODING = 0x20,        /// Set asynchronous line-character formatting properties
    GET_LINE_CODING = 0x21,        /// Read the current line coding
    SET_CONTROL_LINE_STATE = 0x22, /// Generate RS-232 control signals
                                   ///          (wValue = 0b[RTS activation][DTR activation])
    SEND_BREAK = 0x23,             /// Generates an RS-232 style break
                                   ///          (wValue = break length in ms,
                                   ///          if = 0xFFFF it is continuous until
                                   ///          another request with 0 is received)

    SET_RINGER_PARMS = 0x30,
    GET_RINGER_PARMS = 0x31,
    SET_OPERATION_PARMS = 0x32,
    GET_OPERATION_PARMS = 0x33,
    SET_LINE_PARMS = 0x34,
    GET_LINE_PARMS = 0x35,
    DIAL_DIGITS = 0x36,
    SET_UNIT_PARAMETER = 0x37,
    GET_UNIT_PARAMETER = 0x38,
    CLEAR_UNIT_PARAMETER = 0x39,
    GET_PROFILE = 0x3A,

    SET_ENET_MULTICAST_FILTERS =
        0x40, /// Controls the receipt of Ethernet frames
              ///          that are received with multicast destination addresses
    SET_ENET_PWR_MGMT_PFILTER = 0x41, ///
    GET_ENET_PWR_MGMT_PFILTER =
        0x42, /// Retrieves the status of the above power management pattern filter setting
    SET_ENET_PACKET_FILTER =
        0x43, /// Controls the types of Ethernet frames that are to be received via the function
    GET_ENET_STATISTIC =
        0x44, /// Retrieves Ethernet statistics such as
              ///          frames transmitted, frames received, and bad frames received

    SET_ATM_DATA_FORMAT = 0x50,
    GET_ATM_DEVICE_STATISTICS = 0x51,
    SET_ATM_DEFAULT_VC = 0x52,
    GET_ATM_VC_STATISTICS = 0x53,

    GET_NTB_PARAMETERS =
        0x80,               /// Requests the function to report parameters that characterize the NCB
    GET_NET_ADDRESS = 0x81, /// Requests the current EUI-48 network address
    SET_NET_ADDRESS = 0x82, /// Changes the current EUI-48 network address
    GET_NTB_FORMAT = 0x83,  /// Get current NTB Format
    SET_NTB_FORMAT = 0x84,  /// Select 16 or 32 bit Network Transfer Blocks
    GET_NTB_INPUT_SIZE = 0x85,    /// Get the current value of maximum NTB input size
    SET_NTB_INPUT_SIZE = 0x86,    /// Selects the maximum size of NTBs to be transmitted
                                  ///          by the function over the bulk IN pipe
    GET_MAX_DATAGRAM_SIZE = 0x87, /// Requests the current maximum datagram size
    SET_MAX_DATAGRAM_SIZE =
        0x88,            /// Sets the maximum datagram size to a value other than the default
    GET_CRC_MODE = 0x89, /// Requests the current CRC mode
    SET_CRC_MODE = 0x8A, /// Sets the current CRC mode
    GET_EXTENDED_CAPABILITY_MODE = 0x8B,
    SET_EXTENDED_CAPABILITY_MODE = 0x8C,
    GET_EXTENDED_CAPABILITIES = 0x8D,
    GET_EXTENDED_FEATURE = 0x8E,
    SET_EXTENDED_FEATURE = 0x8F,
};

namespace control
{
constexpr usb::control::request_id SET_LINE_CODING{
    direction::OUT, usb::control::request::type::CLASS, usb::control::request::recipient::INTERFACE,
    request::SET_LINE_CODING};

constexpr usb::control::request_id GET_LINE_CODING{
    direction::IN, usb::control::request::type::CLASS, usb::control::request::recipient::INTERFACE,
    request::GET_LINE_CODING};

constexpr usb::control::request_id SET_CONTROL_LINE_STATE{
    direction::OUT, usb::control::request::type::CLASS, usb::control::request::recipient::INTERFACE,
    request::SET_CONTROL_LINE_STATE};

constexpr usb::control::request_id SEND_BREAK{direction::OUT, usb::control::request::type::CLASS,
                                              usb::control::request::recipient::INTERFACE,
                                              request::SEND_BREAK};

constexpr usb::control::request_id GET_NTB_PARAMETERS{
    direction::IN, usb::control::request::type::CLASS, usb::control::request::recipient::INTERFACE,
    request::GET_NTB_PARAMETERS};

constexpr usb::control::request_id SET_NTB_INPUT_SIZE{
    direction::OUT, usb::control::request::type::CLASS, usb::control::request::recipient::INTERFACE,
    request::SET_NTB_INPUT_SIZE};

constexpr usb::control::request_id GET_NTB_INPUT_SIZE{
    direction::IN, usb::control::request::type::CLASS, usb::control::request::recipient::INTERFACE,
    request::GET_NTB_INPUT_SIZE};

constexpr usb::control::request_id SET_NET_ADDRESS{
    direction::OUT, usb::control::request::type::CLASS, usb::control::request::recipient::INTERFACE,
    request::SET_NET_ADDRESS};

constexpr usb::control::request_id GET_NET_ADDRESS{
    direction::IN, usb::control::request::type::CLASS, usb::control::request::recipient::INTERFACE,
    request::GET_NET_ADDRESS};

constexpr usb::control::request_id SET_NTB_FORMAT{
    direction::OUT, usb::control::request::type::CLASS, usb::control::request::recipient::INTERFACE,
    request::SET_NTB_FORMAT};

constexpr usb::control::request_id GET_NTB_FORMAT{direction::IN, usb::control::request::type::CLASS,
                                                  usb::control::request::recipient::INTERFACE,
                                                  request::GET_NTB_FORMAT};
} // namespace control

namespace serial
{
enum class stop_bits : uint8_t
{
    _1 = 0,
    ONE = _1,
    _1p5 = 1,
    ONE_POINT_FIVE = _1p5,
    _2 = 2,
    TWO = _2,
};
enum class parity : uint8_t
{
    NONE = 0,
    ODD = 1,
    EVEN = 2,
    MARK = 3,
    SPACE = 4,
};
struct line_coding
{
    le_uint32_t dwDTERate;   /// Data terminal rate, in bits per second
    stop_bits bCharFormat{}; /// Stop bits:
                             ///     @arg 0 -> 1 Stop bit
                             ///     @arg 1 -> 1.5 Stop bits
                             ///     @arg 2 -> 2 Stop bits
    parity bParityType{};    /// Parity:
                             ///     @arg 0 -> None
                             ///     @arg 1 -> Odd
                             ///     @arg 2 -> Even
                             ///     @arg 3 -> Mark
                             ///     @arg 4 -> Space
    uint8_t bDataBits{};     /// Data bits: (5, 6, 7, 8 or 16)
};

struct state : le_uint16_t
{
    BF_BITS(bool, 0)
    bRxCarrier; /// State of receiver carrier detection mechanism (RS-232 signal DCD)
    BF_BITS(bool, 1) bTxCarrier;  /// State of transmission carrier (RS-232 signal DSR.)
    BF_BITS(bool, 2) bBreak;      /// State of break detection mechanism
    BF_BITS(bool, 3) bRingSignal; /// State of ring signal detection
    BF_BITS(bool, 4) bFraming;    /// A framing error has occurred
    BF_BITS(bool, 5) bParity;     /// A parity error has occurred
    BF_BITS(bool, 6) bOverRun;    /// Received data has been discarded due to overrun
};
} // namespace serial

namespace notification
{
enum class code : uint8_t
{
    NETWORK_CONNECTION = 0x00,
    RESPONSE_AVAILABLE = 0x01,
    AUX_JACK_HOOK_STATE = 0x08,
    RING_DETECT = 0x09,
    SERIAL_STATE = 0x20,
    CALL_STATE_CHANGE = 0x28,
    LINE_STATE_CHANGE = 0x29,
    CONNECTION_SPEED_CHANGE = 0x2A,
    UNIFIED_MEDIUM_STATE = 0x2B,
};

struct header : public usb::control::request
{
    constexpr header(notification::code c, uint16_t value = 0, uint16_t len = 0)
        : request(direction::IN, type::CLASS, recipient::INTERFACE, c, value, len)
    {}
};

struct network_connection : public header
{
    constexpr network_connection(bool connected)
        : header(code::NETWORK_CONNECTION, uint16_t(connected))
    {}
};

struct response_available : public header
{
    constexpr response_available()
        : header(code::RESPONSE_AVAILABLE)
    {}
};

struct speed_change : public header
{
    constexpr speed_change(uint32_t downlink_bitrate_bps, uint32_t uplink_bitrate_bps)
        : header(code::CONNECTION_SPEED_CHANGE, 0, sizeof(speed_change) - sizeof(header)),
          DLBitRate(downlink_bitrate_bps),
          ULBitRate(uplink_bitrate_bps)
    {}
    constexpr speed_change(uint32_t bitrate_bps)
        : speed_change(bitrate_bps, bitrate_bps)
    {}

    le_uint32_t DLBitRate;
    le_uint32_t ULBitRate;
};

struct aux_jack_hook_state : public header
{
    constexpr aux_jack_hook_state(bool off_hook)
        : header(code::AUX_JACK_HOOK_STATE, uint16_t(off_hook))
    {}
};

struct ring_detect : public header
{
    constexpr ring_detect()
        : header(code::RING_DETECT)
    {}
};

struct serial_state : public header
{
    serial_state(serial::state s) // NOLINT(performance-unnecessary-value-param)
        : header(code::SERIAL_STATE, 0, sizeof(serial_state) - sizeof(header)), SerialState(s)
    {}

    serial::state SerialState;
};
} // namespace notification
} // namespace comm

namespace data
{
constexpr uint8_t CLASS_CODE = 0x0A;
constexpr uint8_t SUBCLASS_CODE = 0x00;

enum class protocol_code : uint8_t
{
    USB = 0,
    NCM_NTB = 1, /// Network Transfer Block
};
} // namespace data

namespace ncm
{
constexpr size_t DATAGRAM_MIN_LENGTH = 14;

constexpr version SPEC_VERSION{"1.0"};

enum class formatting : char
{
    IEEE802_3 = '0',
    IEEE802_3_CRC32 = '1',
};

class signature : public le_uint32_t
{
  public:
    constexpr explicit signature(const char (&sig_str)[5]) // NOLINT(modernize-avoid-c-arrays)
    {
        std::copy_n(sig_str, storage.size(), storage.data());
    }
    constexpr signature(size_t bit_size, formatting fmt)
    {
        const char* label = (bit_size == 16) ? "NCM" : "ncm";
        std::copy_n(label, storage.size(), storage.data());
        storage[3] = static_cast<uint8_t>(fmt);
    }
};

// TODO: using packed integers isn't necessary (as all transfers are 32-bit aligned),
// but kept to explicitly specify endianness - this costs packing overhead

namespace ntb
{
enum struct format : uint8_t
{
    NTB16 = 0,
    NTB32 = 1,
};

struct parameters
{
    le_uint16_t wLength{sizeof(parameters)}; /// Size in bytes of this NTBT structure
    struct ntb_formats_support : le_uint16_t
    {
        BF_COPY_SUPERCLASS(ntb_formats_support)
        BF_BITS(bool, 0) ntb16;
        BF_BITS(bool, 1) ntb32;
    } bmNtbFormatsSupported{1}; /// 1 if only 16bit, 3 if 32bit is supported as well
    le_uint32_t dwNtbInMaxSize; /// IN NTB Maximum Size in bytes
    le_uint16_t wNdpInDivisor;  /// Divisor used for IN NTB Datagram payload alignment
    le_uint16_t
        wNdpInPayloadRemainder;  /// Remainder used to align input datagram payload within the NTB
    le_uint16_t wNdpInAlignment; /// Datagram alignment
    reserved_t<2> reserved;
    le_uint32_t dwNtbOutMaxSize;
    le_uint16_t wNdpOutDivisor;
    le_uint16_t wNdpOutPayloadRemainder;
    le_uint16_t wNdpOutAlignment;
    le_uint16_t wNtbOutMaxDatagrams; /// Maximum number of datagrams in a single OUT NTB
};

struct input_size
{
    le_uint32_t dwNtbInMaxSize;
    le_uint16_t wNtbInMaxDatagrams;
    reserved_t<2> reserved;
};

template <size_t BIT_SIZE>
struct header
{
    static_assert((BIT_SIZE == 16) or (BIT_SIZE == 32),
                  "Only 16 and 32 bit NTB formats are defined");

    signature Signature{valid_signature()};   /* (BIT_SIZE == 16) ?"NCMH": "ncmh" */
    le_uint16_t HeaderLength{sizeof(header)}; /* Size in bytes of this NTH16 structure */
    le_uint16_t Sequence;                     /* Sequence number (for debugging) */
    le_uint_t<BIT_SIZE> BlockLength;          /* Size of this NTB in bytes */
    le_uint_t<BIT_SIZE> NdpIndex; /* Offset of the first NDP16 from byte zero of the NTB */

    [[nodiscard]] static constexpr signature valid_signature()
    {
        return signature((BIT_SIZE == 16) ? "NCMH" : "ncmh");
    }
    [[nodiscard]] constexpr bool is_signature_valid() const
    {
        return Signature == valid_signature();
    }
};
static_assert(sizeof(header<16>) == 12);
static_assert(sizeof(header<32>) == 16);

template <size_t BIT_SIZE>
struct datagram_ptr
{
    static_assert((BIT_SIZE == 16) or (BIT_SIZE == 32),
                  "Only 16 and 32 bit NTB formats are defined");

    le_uint_t<BIT_SIZE> DatagramIndex;  /* Offset of the datagram from byte zero of the NTB */
    le_uint_t<BIT_SIZE> DatagramLength; /* Length of the datagram in bytes */
};

template <size_t BIT_SIZE>
struct datagram_pointer_table
{
    static_assert((BIT_SIZE == 16) or (BIT_SIZE == 32),
                  "Only 16 and 32 bit NTB formats are defined");

    signature Signature{valid_signature(formatting::IEEE802_3)};
    le_uint16_t Length; // Size in bytes of this NDP16 structure
    [[no_unique_address]] c2usb::reserved_t<(BIT_SIZE / 8) - 2> Reserved6;
    le_uint_t<BIT_SIZE> NextNdpIndex; // Offset of the next NDP16 from byte zero of the NTB
    [[no_unique_address]] c2usb::reserved_t<((BIT_SIZE / 8) - 2) * 2> Reserved12;
    // NOLINTNEXTLINE(modernize-avoid-c-arrays)
    datagram_ptr<BIT_SIZE> Datagram[1]; // The last datagram index + header is always 0

    [[nodiscard]] static constexpr signature valid_signature(formatting fmt = formatting::IEEE802_3)
    {
        return {BIT_SIZE, fmt};
    }
    [[nodiscard]] constexpr bool is_signature_valid(formatting fmt = formatting::IEEE802_3) const
    {
        return Signature == valid_signature(fmt);
    }
};
static_assert(sizeof(datagram_pointer_table<16>) == 12);
static_assert(sizeof(datagram_pointer_table<32>) == 24);

template <size_t BIT_SIZE>
constexpr size_t min_size()
{
    return sizeof(header<BIT_SIZE>) + sizeof(datagram_pointer_table<BIT_SIZE>) +
           sizeof(datagram_ptr<BIT_SIZE>) + DATAGRAM_MIN_LENGTH;
}

} // namespace ntb
} // namespace ncm

using mac_address = std::array<uint8_t, 6>;

namespace descriptor
{
/// @brief CDC class descriptor types
enum class type : uint8_t
{
    INTERFACE = 0x24,
    ENDPOINT = 0x25,
};

namespace comm
{
enum class type : uint8_t
{
    HEADER = 0x00,
    CALL_MANAGEMENT = 0x01,
    ABSTRACT_CONTROL_MANAGEMENT = 0x02,
    DIRECT_LINE_MANAGEMENT = 0x03,
    TELEPHONE_RINGER = 0x04,
    TELEPHONE_CALL_AND_LINE_STATE_REPORTING_CAPABILITIES = 0x05,
    UNION = 0x06,
    COUNTRY_SELECTION = 0x07,
    TELEPHONE_OPERATIONAL_MODES = 0x08,
    USB_TERMINAL = 0x09,
    NETWORK_CHANNEL_TERMINAL = 0x0A,
    PROTOCOL_UNIT = 0x0B,
    EXTENSION_UNIT = 0x0C,
    MULTI_CHANNEL_MANAGEMENT = 0x0D,
    CAPI_CONTROL_MANAGEMENT = 0x0E,
    ETHERNET_NETWORKING = 0x0F,
    ATM_NETWORKING = 0x10,
    WIRELESS_HANDSET_CONTROL_MODEL = 0x11,
    MOBILE_DIRECT_LINE_MODEL = 0x12,
    MDLM_DETAIL = 0x13,
    DEVICE_MANAGEMENT_MODEL = 0x14,
    OBEX = 0x15,
    COMMAND_SET = 0x16,
    COMMAND_SET_DETAIL = 0x17,
    TELEPHONE_CONTROL_MODEL = 0x18,
    OBEX_SERVICE_IDENTIFIER = 0x19,
    NCM = 0x1A,
    MBIM_FUNCTIONAL = 0x1B,
    MBIM_EXTENDED_FUNCTIONAL = 0x1C,
    NCM_EXTENDED_CAPABILITY = 0x1D,
    NCM_EXTENDED_FEATURE = 0x1E
};
}
namespace data
{
enum class type : uint8_t
{
    HEADER = 0x00,
};
}

template <class T, typename TEnum = comm::type>
struct descriptor : public usb::descriptor<T>
{
    constexpr static auto TYPE_CODE = cdc::descriptor::type::INTERFACE;

    constexpr static TEnum functional_type() { return (T::FUNC_TYPE_CODE); }

    uint8_t bDescriptorSubtype{static_cast<uint8_t>(functional_type())};
};

struct header : public descriptor<header>
{
    constexpr static auto FUNC_TYPE_CODE = comm::type::HEADER;

    version bcdCDC{cdc::SPEC_VERSION};
};

template <size_t SIZE = 1>
struct union_ : public descriptor<union_<SIZE>>
{
    constexpr static auto FUNC_TYPE_CODE = comm::type::UNION;
    constexpr static auto interface_count() { return 1 + SIZE; }

    uint8_t bControlInterface{};
    std::array<uint8_t, SIZE> bSubordinateInterfaces{};

    constexpr union_() = default;
    constexpr union_(uint8_t first_if)
        : bControlInterface(first_if)
    {
        for (auto& next_if : bSubordinateInterfaces)
        {
            next_if = ++first_if;
        }
    }
};

struct call_management : public descriptor<call_management>
{
    constexpr static auto FUNC_TYPE_CODE = comm::type::CALL_MANAGEMENT;

    uint8_t bmCapabilities{};
    uint8_t bDataInterface{};
};

struct abstract_control_management : public descriptor<abstract_control_management>
{
    constexpr static auto FUNC_TYPE_CODE = comm::type::ABSTRACT_CONTROL_MANAGEMENT;

    struct capabilities : bitfilled::host_integer<uint8_t>
    {
        BF_BITS(bool, 0)
        comm_feature; /// Device supports the request combination of
                      /// Set_Comm_Feature, Clear_Comm_Feature, and Get_Comm_Feature
        BF_BITS(bool, 1)
        line_control;                /// Device supports the request combination of
                                     /// Set_Line_Coding, Set_Control_Line_State, Get_Line_Coding,
                                     /// and the notification Serial_State
        BF_BITS(bool, 2) send_break; /// Device supports the request Send_Break
        BF_BITS(bool, 3) network_connection; /// Device supports the notification Network_Connection
    } bmCapabilities;
};

struct ethernet_networking : public descriptor<ethernet_networking>
{
    constexpr static auto FUNC_TYPE_CODE = comm::type::ETHERNET_NETWORKING;
    constexpr static size_t DEFAULT_MAX_SEGMENT_SIZE = 1514;

    istring iMACAddress{}; /// Index of string descriptor. The string descriptor holds the 48bit
                           /// Ethernet MAC address.
    le_uint32_t bmEthernetStatistics; /// Indicates which Ethernet statistics functions the device
                                      /// collects.
    le_uint16_t wMaxSegmentSize{
        DEFAULT_MAX_SEGMENT_SIZE}; /// Typically 1514 bytes, but could be extended.
    le_uint16_t
        wNumberMCFilters; /// Number of multicast filters that can be configured by the host.
    uint8_t bNumberPowerFilters{}; /// Number of pattern filters that are available for causing
                                   /// wake-up of the host.
};

struct network_control : public descriptor<network_control>
{
    constexpr static auto FUNC_TYPE_CODE = comm::type::NCM;

    version bcdNcmVersion{ncm::SPEC_VERSION};

    struct capabilities : bitfilled::host_integer<uint8_t>
    {
        BF_BITS(bool, 0) eth_packet_filter; /// SetEthernetPacketFilter requests.
        BF_BITS(bool, 1) net_address;       /// GetNetAddress and SetNetAddress requests.
        BF_BITS(bool, 2)
        encapsulated_cmd; /// SendEncapsulated-Command and GetEncapsulatedResponse requests.
        BF_BITS(bool, 3) max_datagram_size; /// SetMaxDatagramSize and GetMaxDatagramSize requests.
        BF_BITS(bool, 4) crc_mode;          /// SetCrcMode and GetCrcMode requests.
        BF_BITS(bool, 5)
        ntb_input_size_8byte; /// 8-byte forms of GetNtbInputSize and SetNtbInputSize requests.
    } bmNetworkCapabilities{};
};
} // namespace descriptor
} // namespace usb::cdc
