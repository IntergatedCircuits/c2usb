/// @file
///
/// UHK patch: injectable logging + port-agnostic anomaly diagnostics for MACs.
///
/// MAC ports record every anomaly (enqueue failures, EP errors, control errors,
/// stalls, dropped events, rejected transfers) together with bus-level events
/// (reset/suspend/resume/Vbus). Counters distinguish totals since boot from
/// occurrences since the last successful data-endpoint transfer, so a dump
/// answers "what went wrong since USB last worked".
///
/// The host application may provide a printf-like `c2usb_log()` and a
/// millisecond time source `c2usb_diag_time_ms()`; weak defaults keep c2usb
/// standalone-linkable.
///
#ifndef __USB_DF_MAC_DIAG_HPP_
#define __USB_DF_MAC_DIAG_HPP_

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// Printf-like log output, overridable by the application (weak no-op default).
    void c2usb_log(const char* fmt, ...);

    /// Millisecond timestamp source, overridable by the application or port
    /// (weak default returns 0).
    uint32_t c2usb_diag_time_ms(void);

    /// Dump current MAC state and all recorded anomalies via c2usb_log().
    void c2usb_diag_dump(void);

    /// Clear all recorded diagnostics.
    void c2usb_diag_reset(void);

    /// Port-specific extra dump output (weak no-op default).
    void c2usb_diag_dump_port(void);

#ifdef __cplusplus
}

namespace usb::df
{
class mac;

namespace diag
{
enum category : uint8_t
{
    ENQUEUE_FAIL = 0, // handing a transfer to the controller driver failed
    EP_ERROR,         // data endpoint transfer failed / was cancelled
    CTRL_ERROR,       // control endpoint event with error
    CTRL_STALL,       // control pipeline stalled by the MAC
    EVENT_DROPPED,    // driver event lost (e.g. queue full)
    EP_BUSY,          // transfer rejected: endpoint busy
    EP_NO_POWER,      // transfer rejected: bus not in L0
    CATEGORY_COUNT
};

enum class bus_event : uint8_t
{
    RESET = 0,
    SUSPEND,
    RESUME,
    SLEEP,
    VBUS_ON,
    VBUS_OFF,
    ERROR,
    SET_CONFIG,   // host-initiated SET_CONFIGURATION with a nonzero config
    DECONFIG,     // host-initiated SET_CONFIGURATION 0 (configuration cleared)
};

// all callable from any context including ISR - they must not log
void record(category cat, uint8_t ep, int err);
void success(uint8_t ep);
void bus(bus_event event);
void set_mac(mac* m);
} // namespace diag
} // namespace usb::df
#endif

#endif // __USB_DF_MAC_DIAG_HPP_
