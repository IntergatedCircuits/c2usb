// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "usb/control.hpp"
#include "usb/version.hpp"
#include <bitfilled.hpp>

namespace usb::dfu
{
constexpr uint8_t CLASS_CODE = 0xFE;
constexpr uint8_t SUBCLASS_CODE = 0x01;
constexpr version SPEC_VERSION{"1.1.0"};

enum struct request : uint8_t
{
    DETACH = 0,    /// Detach the application from the USB device
    DNLOAD = 1,    /// Download a block to the program memory
    UPLOAD = 2,    /// Read a block of the program memory
    GETSTATUS = 3, /// Read the DFU status
    CLRSTATUS = 4, /// Clear the error status
    GETSTATE = 5,  /// Read the DFU state
    ABORT = 6,     /// Abort the ongoing request
};

namespace control
{
constexpr usb::control::request_id DETACH{direction::OUT, usb::control::request::type::CLASS,
                                          usb::control::request::recipient::INTERFACE,
                                          request::DETACH};

constexpr usb::control::request_id DNLOAD{direction::OUT, usb::control::request::type::CLASS,
                                          usb::control::request::recipient::INTERFACE,
                                          request::DNLOAD};

constexpr usb::control::request_id UPLOAD{direction::IN, usb::control::request::type::CLASS,
                                          usb::control::request::recipient::INTERFACE,
                                          request::UPLOAD};

constexpr usb::control::request_id GETSTATUS{direction::IN, usb::control::request::type::CLASS,
                                             usb::control::request::recipient::INTERFACE,
                                             request::GETSTATUS};

constexpr usb::control::request_id CLRSTATUS{direction::OUT, usb::control::request::type::CLASS,
                                             usb::control::request::recipient::INTERFACE,
                                             request::CLRSTATUS};

constexpr usb::control::request_id GETSTATE{direction::IN, usb::control::request::type::CLASS,
                                            usb::control::request::recipient::INTERFACE,
                                            request::GETSTATE};

constexpr usb::control::request_id ABORT{direction::OUT, usb::control::request::type::CLASS,
                                         usb::control::request::recipient::INTERFACE,
                                         request::ABORT};
} // namespace control

enum struct mode : uint8_t
{
    RUNTIME = 1,
    DFU = 2,
};

enum struct state : uint8_t
{
    APP_IDLE = 0,      /// Device is running its normal application.
    APP_DETACH = 1,    /// Device is running its normal application, has received the DFU_DETACH
                       /// request, and is waiting for a USB reset.
    IDLE = 2,          /// Device is waiting for requests in DFU mode.
    DNLOAD_SYNC = 3,   /// Device has received a block and is waiting for the host
                       /// to solicit the status via DFU_GETSTATUS.
    DNLOAD_BUSY = 4,   /// Device is programming a control-write block into its
                       /// non-volatile memories.
    DNLOAD_IDLE = 5,   /// Device is processing a download operation.
    MANIFEST_SYNC = 6, /// Device has received the final block of firmware
                       /// from the host and is waiting for receipt of
                       /// DFU_GETSTATUS to begin the Manifestation phase;
                       /// or device has completed the Manifestation phase and
                       /// is waiting for receipt of DFU_GETSTATUS.
    MANIFEST = 7,      /// Device is in the Manifestation phase.
    MANIFEST_WAIT_RESET = 8, /// Device has programmed its memories and is waiting for a
                             /// USB reset or a power on reset.
    UPLOAD_IDLE = 9,         /// The device is processing an upload operation.
    ERROR = 10,              /// An error has occurred.
};

bool valid_state_transition(state current, request req);

enum struct error : uint8_t
{
    NONE = 0x00,         /// No error condition is present.
    TARGET = 0x01,       /// File is not targeted for use by this device.
    FILE = 0x02,         /// File is for this device but fails some vendor-specific
                         /// verification test.
    WRITE = 0x03,        /// Device is unable to write memory.
    ERASE = 0x04,        /// Memory erase function failed.
    CHECK_ERASED = 0x05, /// Memory erase check failed.
    PROG = 0x06,         /// Program memory function failed.
    VERIFY = 0x07,       /// Programmed memory failed verification.
    ADDRESS = 0x08,      /// Cannot program memory due to received address that is
                         /// out of range.
    NOTDONE = 0x09,      /// Received DFU_DNLOAD with wLength = 0, but device does
                         /// not think it has all of the data yet.
    FIRMWARE = 0x0A,     /// Device's firmware is corrupt. It cannot return to run-time
                         /// (non-DFU) operations.
    VENDOR = 0x0B,       /// iString indicates a vendor-specific error.
    USB = 0x0C,          /// Device detected unexpected USB reset signaling.
    POR = 0x0D,          /// Device detected unexpected power on reset.
    UNKNOWN = 0x0E,      /// Something went wrong.
    STALLEDPKT = 0x0F,   /// Device stalled an unexpected request.
};

struct status
{
    error bStatus{};             /// Status resulting from the execution of the most recent request.
    le_uint_t<24> bwPollTimeout; /// Minimum time in milliseconds that the host should wait
                                 /// before sending a subsequent DFU_GETSTATUS request.
    state bState{};              /// State that the device is going to enter immediately following
                                 /// transmission of this response.
    istring iString{};           /// Index of status description in string table.
};

namespace descriptor
{
enum class type : uint8_t
{
    FUNCTIONAL = 0x21, /// DFU functional descriptor
};

struct functional : public usb::descriptor<functional>
{
    constexpr static auto TYPE_CODE = type::FUNCTIONAL;

    struct attributes : bitfilled::host_integer<uint8_t>
    {
        BF_COPY_SUPERCLASS(attributes)
        BF_BITS(bool, 0) can_download; /// Device is capable of the download operation.
        BF_BITS(bool, 1) can_upload;   /// Device is capable of the upload operation.
        BF_BITS(bool, 2)
        manifestation_tolerant;       /// Device is able to communicate without a USB reset
                                      /// after the manifestation phase is complete.
        BF_BITS(bool, 3) will_detach; /// Device will detach when it receives a DFU_DETACH  request.
    } bmAttributes{};
    le_uint16_t wDetachTimeOut; /// milliseconds
    le_uint16_t wTransferSize;
    version bcdDFUVersion{SPEC_VERSION};
};

} // namespace descriptor

} // namespace usb::dfu
