/// @author Benedek Kupper
/// @date   2024
///
/// @copyright
///         This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
///         If a copy of the MPL was not distributed with this file, You can obtain one at
///         https://mozilla.org/MPL/2.0/.
///
#ifndef __PORT_ZEPHYR_MESSAGE_QUEUE_HPP
#define __PORT_ZEPHYR_MESSAGE_QUEUE_HPP

#include <cstddef>
#include <optional>
#include "port/zephyr/tick_timer.hpp"

#if C2USB_HAS_ZEPHYR_KERNEL

namespace os::zephyr
{
template <typename T, std::size_t SIZE>
class message_queue
{
  public:
    message_queue() { ::k_msgq_init(&msgq_, msgq_buffer_, sizeof(T), max_size()); }

    void post(const T& msg)
    {
        [[maybe_unused]] auto ret = ::k_msgq_put(&msgq_, &msg, K_FOREVER);
        assert(ret == 0);
    }
    bool try_post(const T& msg) { return ::k_msgq_put(&msgq_, &msg, K_NO_WAIT) == 0; }
    template <class Rep, class Period>
    inline bool try_post_for(const T& msg, const std::chrono::duration<Rep, Period>& rel_time)
    {
        return ::k_msgq_put(&msgq_, &msg, to_timeout(rel_time)) == 0;
    }
    template <class Clock, class Duration>
    inline bool try_post_until(const T& msg,
                               const std::chrono::time_point<Clock, Duration>& abs_time)
    {
        return try_post_for(abs_time - Clock::now());
    }

    T get()
    {
        T msg;
        [[maybe_unused]] auto ret = ::k_msgq_get(&msgq_, reinterpret_cast<void*>(&msg), K_FOREVER);
        assert(ret == 0);
        return msg;
    }
    std::optional<T> try_get()
    {
        T msg;
        if (::k_msgq_get(&msgq_, reinterpret_cast<void*>(&msg), K_NO_WAIT) == 0)
        {
            return msg;
        }
        return std::nullopt;
    }

    void flush() { ::k_msgq_purge(&msgq_); }

    std::optional<T> peek() const
    {
        T msg;
        if (::k_msgq_peek(const_cast<::k_msgq*>(&msgq_), reinterpret_cast<void*>(&msg)) == 0)
        {
            return msg;
        }
        return std::nullopt;
    }

    std::size_t size() const { return ::k_msgq_num_used_get(const_cast<::k_msgq*>(&msgq_)); }
    static constexpr std::size_t max_size() { return SIZE; }
    bool empty() const { return ::k_msgq_num_used_get(const_cast<::k_msgq*>(&msgq_)) == 0; }
    bool full() const { return ::k_msgq_num_free_get(const_cast<::k_msgq*>(&msgq_)) == 0; }

  private:
    ::k_msgq msgq_;
    char msgq_buffer_[max_size() * sizeof(T)];
};

} // namespace os::zephyr

#endif // C2USB_HAS_ZEPHYR_KERNEL

#endif // __PORT_ZEPHYR_MESSAGE_QUEUE_HPP
