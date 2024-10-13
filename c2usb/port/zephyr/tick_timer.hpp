/// @author Benedek Kupper
/// @date   2024
///
/// @copyright
///         This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
///         If a copy of the MPL was not distributed with this file, You can obtain one at
///         https://mozilla.org/MPL/2.0/.
///
#ifndef __PORT_ZEPHYR_TICK_TIMER_HPP
#define __PORT_ZEPHYR_TICK_TIMER_HPP

#define C2USB_HAS_ZEPHYR_KERNEL    (__has_include("zephyr/kernel.h"))
#if C2USB_HAS_ZEPHYR_KERNEL

#include <chrono>
#include <zephyr/kernel.h>

namespace os::zephyr
{
class tick_timer
{
  public:
    using rep = ::k_ticks_t;
    using period = std::ratio<1, Z_HZ_ticks>;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<tick_timer>;
    static constexpr bool is_steady = true;

    static time_point now() { return time_point{duration{::k_uptime_ticks()}}; }
};

template <class Rep, class Period>
constexpr tick_timer::rep to_ticks(const std::chrono::duration<Rep, Period>& rel_time)
{
    return std::chrono::duration_cast<tick_timer::duration>(rel_time).count();
}

constexpr tick_timer::rep to_ticks(const tick_timer::time_point& time)
{
    return to_ticks(time.time_since_epoch());
}

template <class Rep, class Period>
constexpr k_timeout_t to_timeout(const std::chrono::duration<Rep, Period>& rel_time)
{
    return Z_TIMEOUT_TICKS(to_ticks(rel_time));
}

constexpr k_timeout_t to_timeout(const tick_timer::time_point& time)
{
    return K_TIMEOUT_ABS_TICKS(to_ticks(time.time_since_epoch()));
}

constexpr inline tick_timer::duration infinity{K_TICKS_FOREVER};

} // namespace os::zephyr

#endif // C2USB_HAS_ZEPHYR_KERNEL

#endif // __PORT_ZEPHYR_TICK_TIMER_HPP
