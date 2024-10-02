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
#include <zephyr/kernel.h>

namespace os::zephyr
{
template <typename T, std::size_t SIZE>
class message_queue
{
  public:
    message_queue() { ::k_msgq_init(&msgq_, msgq_buffer_, sizeof(T), max_size()); }

    bool post(const T& msg, ::k_timeout_t timeout = K_FOREVER)
    {
        return ::k_msgq_put(&msgq_, &msg, timeout) == 0;
    }

    std::optional<T> get(::k_timeout_t timeout = K_FOREVER)
    {
        T msg;
        if (::k_msgq_get(&msgq_, reinterpret_cast<void*>(&msg), timeout) == 0)
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

#endif // __PORT_ZEPHYR_MESSAGE_QUEUE_HPP