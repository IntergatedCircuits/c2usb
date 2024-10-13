/// @author Benedek Kupper
/// @date   2024
///
/// @copyright
///         This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
///         If a copy of the MPL was not distributed with this file, You can obtain one at
///         https://mozilla.org/MPL/2.0/.
///
#ifndef __PORT_ZEPHYR_SEMAPHORE_HPP
#define __PORT_ZEPHYR_SEMAPHORE_HPP

#include "port/zephyr/tick_timer.hpp"

#if C2USB_HAS_ZEPHYR_KERNEL

namespace os::zephyr
{
template <std::ptrdiff_t COUNT>
class counting_semaphore
{
  public:
    counting_semaphore(std::ptrdiff_t desired)
    {
        assert((desired >= 0) and (desired <= COUNT));
        ::k_sem_init(&sem_, desired, COUNT);
    }
    void release(std::ptrdiff_t update = 1)
    {
        assert(update >= 0);
        for (; update > 0; --update)
        {
            ::k_sem_give(&sem_);
        }
    }
    void acquire() { ::k_sem_take(&sem_, K_FOREVER); }
    bool try_acquire() { return ::k_sem_take(&sem_, K_NO_WAIT) == 0; }
    template <class Rep, class Period>
    inline bool try_acquire_for(const std::chrono::duration<Rep, Period>& rel_time)
    {
        return ::k_sem_take(&sem_, to_timeout(rel_time)) == 0;
    }
    template <class Clock, class Duration>
    inline bool try_acquire_until(const std::chrono::time_point<Clock, Duration>& abs_time)
    {
        return try_acquire_for(abs_time - Clock::now());
    }

  private:
    ::k_sem sem_;
};

using binary_semaphore = counting_semaphore<1>;

} // namespace os::zephyr

#endif // C2USB_HAS_ZEPHYR_KERNEL

#endif // __PORT_ZEPHYR_SEMAPHORE_HPP
