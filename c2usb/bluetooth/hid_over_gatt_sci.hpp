// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cassert>
#include <chrono>
#include "c2usb.hpp"
#if __has_include("zephyr/bluetooth/conn.h")
#include <zephyr/bluetooth/conn.h>
#else
struct bt_conn_le_conn_rate_param
{
    uint16_t interval_min_125us;
    uint16_t interval_max_125us;
    /* The subrate factor allows the Central and Peripheral to use a reduced number of
    connection events. The Central shall only transmit on subrated connection events,
    the events specified in Section 5.1.1, and, if the continuation number is non-zero,
    continuation events. */
    uint16_t subrate_min;
    uint16_t subrate_max;
    /* Peripheral latency also allows a Peripheral to use a reduced number of connection
    events. The connPeripheralLatency parameter defines the number of consecutive
    subrated connection events that the Peripheral is not required to listen for the
    Central. For example, if connSubrateFactor is 3, connContinuationNumber is 0, and
    connPeripheralLatency is 6, then a Peripheral implementation can choose to only listen
    to every 21st connection event (i.e., every 7th subrated connection event). */
    uint16_t max_latency;
    /* A continuation event is a connection event where, in at least one of the previous
    connContinuationNumber connection events (ignoring any before the last subrated
    connection event), at least one packet was transmitted or validly received containing
    a Link Layer PDU with a non-zero Length field. Continuation events are determined by
    activity in a subrated connection event and any subsequent continuation events. Some
    connection events between two consecutive subrated connection events might not be
    continuation events. The value of connContinuationNumber shall be in the range 0 to
    connSubrateFactor - 1 */
    uint16_t continuation_number;
    /* Connection supervision timeout (connSupervisionTimeout) is a parameter that
    defines the maximum time between two received Data Channel PDUs or
    Connected Isochronous PDUs before the connection is considered lost. */
    uint16_t supervision_timeout_10ms;
    uint16_t min_ce_len_125us;
    uint16_t max_ce_len_125us;
};
#endif

namespace bluetooth
{

/// @brief  A duration type representing the unit of time used for Bluetooth connection events,
///         which is 125 microseconds per count.
template <typename T = uint16_t>
using conn_event_unit = std::chrono::duration<T, std::ratio<125, 1'000'000>>;

/// @brief  A duration type representing the unit of time used for Bluetooth connection supervision
///         timeout, which is 10 milliseconds per count.
template <typename T = uint16_t>
using supervision_timeout_unit = std::chrono::duration<T, std::ratio<10, 1'000>>;

namespace hid_over_gatt
{

/// @brief  HID over GATT supported Shorter Connection Interval (SCI) modes
enum class sci_mode : uint8_t
{
    NONE = 0,
    DEFAULT = 2,
    FAST = 3,
    LOW_POWER = 4,
    FULL_RANGE = 5,
};

/// @brief  HID over GATT Shorter Connection Interval (SCI) mode parameters,
///         see HID Over GATT Profile 7.4.1 and BT Core specification Vol 6, Part B, Section 4.5.1
struct sci_mode_params : public ::bt_conn_le_conn_rate_param
{
    sci_mode mode;

    /// @brief Constructor for manually specifying all parameters for a given SCI mode.
    /// @param m: the sci_mode to which these parameters apply
    /// @param interval_min Minimum connection interval, between 375 us and 4000 ms
    /// @param interval_max Maximum connection interval, between 375 us and 4000 ms
    /// @param subrate_min Minimum subrate factor, between 1 and 500
    /// @param subrate_max Maximum subrate factor, between 1 and 500
    /// @param max_latency Maximum peripheral latency, between 0 and 499
    /// @param continuation_number Minimum number of connection events to remain active, between 0
    /// and 499
    /// @param supervision_timeout Connection supervision timeout, between 100 ms and 32 s
    /// @param min_ce_len Minimum connection event length, between 125 us and 1'999'875 us
    /// @param max_ce_len Maximum connection event length, between 125 us and 1'999'875 us
    template <typename TInterval, typename MinInterval, typename MaxInterval, typename TST,
              typename STRatio, typename TCe, typename MinCeLen, typename MaxCeLen>
    constexpr sci_mode_params(sci_mode m,
                              std::chrono::duration<TInterval, MinInterval> interval_min,
                              std::chrono::duration<TInterval, MaxInterval> interval_max,
                              uint16_t subrate_min, uint16_t subrate_max, uint16_t max_latency,
                              uint16_t continuation_number,
                              std::chrono::duration<TST, STRatio> supervision_timeout,
                              std::chrono::duration<TCe, MinCeLen> min_ce_len,
                              std::chrono::duration<TCe, MaxCeLen> max_ce_len)
        : bt_conn_le_conn_rate_param{std::chrono::round<conn_event_unit<uint16_t>>(interval_min)
                                         .count(),
                                     std::chrono::round<conn_event_unit<uint16_t>>(interval_max)
                                         .count(),
                                     subrate_min,
                                     subrate_max,
                                     max_latency,
                                     continuation_number,
                                     std::chrono::round<supervision_timeout_unit<uint16_t>>(
                                         supervision_timeout)
                                         .count(),
                                     std::chrono::round<conn_event_unit<uint16_t>>(min_ce_len)
                                         .count(),
                                     std::chrono::round<conn_event_unit<uint16_t>>(max_ce_len)
                                         .count()},
          mode(m)
    {
        assert((interval_min <= interval_max) and
               (interval_min >= std::chrono::microseconds{375}) and
               (interval_max <= std::chrono::milliseconds{4000}));
        assert((subrate_min <= subrate_max) and (subrate_min >= 1) and (subrate_max <= 500));
        assert(continuation_number < subrate_max);
        assert(latency_valid());
        // assert(supervision_timeout_valid());
        assert((min_ce_len <= max_ce_len) and (min_ce_len >= std::chrono::microseconds{125}) and
               (max_ce_len <= std::chrono::microseconds{1'999'875}));
    }

    /// @brief  A simplified constructor, using the full range of connection event lengths.
    template <typename T, typename MinInterval, typename MaxInterval, typename TST,
              typename STRatio>
    constexpr sci_mode_params(sci_mode m, std::chrono::duration<T, MinInterval> interval_min,
                              std::chrono::duration<T, MaxInterval> interval_max,
                              uint16_t subrate_min, uint16_t subrate_max, uint16_t max_latency,
                              uint16_t continuation_number,
                              std::chrono::duration<TST, STRatio> supervision_timeout)
        : sci_mode_params(m, interval_min, interval_max, subrate_min, subrate_max, max_latency,
                          continuation_number, supervision_timeout, std::chrono::microseconds{125},
                          std::chrono::microseconds{1'999'875})
    {}

    /// @brief  A simplified constructor, using the shortest allowed supervision timeout and
    ///         the full range of connection event lengths.
    template <typename T, typename MinInterval, typename MaxInterval>
    constexpr sci_mode_params(sci_mode m, std::chrono::duration<T, MinInterval> interval_min,
                              std::chrono::duration<T, MaxInterval> interval_max,
                              uint16_t subrate_min, uint16_t subrate_max, uint16_t max_latency,
                              uint16_t continuation_number)
        : sci_mode_params(m, interval_min, interval_max, subrate_min, subrate_max, max_latency,
                          continuation_number,
                          shortest_supervision_timeout(interval_max, subrate_max, max_latency))
    {}

    constexpr bool latency_valid() const
    {
        // requirement from BT Core specification Vol 6, Part B, Section 4.5.1
        return (max_latency <= 499) and (subrate_max * (max_latency + 1) <= 500);
    }

    constexpr bool supervision_timeout_valid() const
    {
        using namespace std::chrono_literals;
        // requirement from BT Core specification Vol 6, Part B, Section 4.5.1
        // connInterval * connSubrateFactor * (connPeripheralLatency + 1) is less than half
        // connSupervisionTimeout.
        auto lower_limit = std::chrono::duration_cast<std::chrono::microseconds>(
                               conn_event_unit<uint16_t>{interval_max_125us}) *
                               subrate_max * (max_latency + 1) * 2 +
                           1us;
        const auto timeout = supervision_timeout_unit<uint16_t>{supervision_timeout_10ms};
        return (timeout >= std::max<std::chrono::microseconds>(100ms, lower_limit)) and
               (timeout <= 32s);
    }

    template <typename T, typename MaxInterval>
    static constexpr auto
    shortest_supervision_timeout(std::chrono::duration<T, MaxInterval> interval_max,
                                 uint16_t subrate_max, uint16_t max_latency)
    {
        using namespace std::chrono_literals;
        auto limit = std::chrono::duration_cast<std::chrono::microseconds>(interval_max) *
                     subrate_max * (max_latency + 1) * 2;
        return std::max(std::chrono::ceil<supervision_timeout_unit<uint16_t>>(
                            limit + std::chrono::microseconds(1)),
                        std::chrono::duration_cast<supervision_timeout_unit<uint16_t>>(100ms));
    }

    // recommended default parameters for each mode
    static constexpr auto defaults()
    {
        using namespace std::chrono_literals;
        return sci_mode_params(sci_mode::DEFAULT, 7'500us, 15ms, 1, 4, 0, 0);
    }
    static constexpr auto fast()
    {
        // the Connection Interval min value and Connection Interval max value shall be less
        // than 7.5 ms.
        using namespace std::chrono_literals;
        return sci_mode_params(sci_mode::FAST, 1'250us, 5ms, 1, 4, 0, 3);
    }
    static constexpr auto low_power()
    {
        using namespace std::chrono_literals;
        return sci_mode_params(sci_mode::LOW_POWER, 7'500us, 15ms, 1, 4, 100, 0);
    }
    static constexpr auto full_range()
    {
        // the Connection Interval min value shall be the same as the Connection Interval min value
        // in the Fast mode.
        using namespace std::chrono_literals;
        return sci_mode_params(sci_mode::FULL_RANGE, 1'250us, 15ms, 1, 4, 0, 1);
    }

    /// @brief  This is the specification recommended set of SCI modes and their parameters.
    /// @return A span of the recommended SCI mode parameters
    static std::span<const sci_mode_params> recommended_set()
    {
        static constexpr std::array<sci_mode_params, 4> modes{
            sci_mode_params::defaults(), sci_mode_params::fast(), sci_mode_params::low_power(),
            sci_mode_params::full_range()};
        return modes;
    }

    template <typename T> // ::bt_conn_le_conn_rate_changed
    constexpr bool match(const T& params) const
    {
        const auto interval = std::chrono::microseconds(params.interval_us);
        bool interval_match = (conn_event_unit<>(interval_min_125us) <= interval) and
                              (interval <= conn_event_unit<>(interval_max_125us));
        bool subrate_match =
            (subrate_min <= params.subrate_factor) and (params.subrate_factor <= subrate_max);
        bool latency_match = params.peripheral_latency <= max_latency;
        // TODO continuation_number
        // TODO supervision_timeout_10ms
        return interval_match and subrate_match and latency_match;
    }
};

struct sci_range
{
    // unit 125us (Time range: 375 μs to 4 s)
    c2usb::le_uint16_t min{};    // Minimum supported connection interval
    c2usb::le_uint16_t max{};    // Maximum supported connection interval
    c2usb::le_uint16_t stride{}; // The connection interval resolution
};

template <size_t NUM_GROUPS>
struct sci_attributes
{
    uint8_t min_supported_conn_interval{}; // unit 125us (Time Range: 375 μs to 7.5 ms)
    uint8_t num_groups{};
    std::array<sci_range, NUM_GROUPS> groups{};
};

} // namespace hid_over_gatt

} // namespace bluetooth
