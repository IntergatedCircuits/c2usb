// SPDX-License-Identifier: MPL-2.0
#include <algorithm>
#include <cassert>
#include "usb/serial_buffer.hpp"

namespace usb
{
std::optional<std::span<const uint8_t>> serial_tx_buffer::write(const uint8_t* data, size_t* length)
{
    size_type idx{};
    size_type pos{pos_[idx].load()};
    size_type new_pos;  // NOLINT(*-init-variables)
    size_type writable; // NOLINT(*-init-variables)
    do                  // NOLINT(*-avoid-do-while)
    {
        while ((pos & consuming_flag) != 0)
        {
            idx = size_type{1} - idx;
            pos = pos_[idx].load();
        }
        new_pos = pos;
        writable = std::min(size() - pos, *length);
        if (writable == 0)
        {
            break;
        }
        std::copy_n(data, writable,
                    &buffer_of(idx)[new_pos]); // NOLINT(*-bounds-pointer-arithmetic)
        new_pos += writable;
    } while (not pos_[idx].compare_exchange_weak(pos, new_pos));

    *length = writable;
    if ((writable > 0) and (pos_[size_type{1} - idx].load() == 0))
    {
        if (pos_[idx].compare_exchange_strong(new_pos, new_pos | consuming_flag))
        {
            return std::span<const uint8_t>{buffer_of(idx), new_pos};
        }
    }
    return std::nullopt;
}

void serial_tx_buffer::cancel_consume(const std::span<const uint8_t>& data)
{
    size_type idx = data.data() >= buffer_of(1) ? 1 : 0;
    size_type pos = static_cast<size_type>(data.data() - buffer_of(idx)) + data.size();
    pos |= consuming_flag;
    if (!pos_[idx].compare_exchange_strong(pos, pos & ~consuming_flag))
    {
        assert(false);
    }
}

std::optional<std::span<const uint8_t>>
serial_tx_buffer::advance(const std::span<const uint8_t>& data, bool needs_zlp)
{
    size_type idx = data.data() >= buffer_of(1) ? 1 : 0;
    size_type pos = static_cast<size_type>(data.data() - buffer_of(idx)) + data.size();
    size_type other_idx = size_type{1} - idx;
    size_type new_pos = pos_[other_idx].load();
    if (needs_zlp and (new_pos == 0))
    {
        return data.subspan(data.size());
    }

    pos |= consuming_flag;
    [[maybe_unused]] bool success = pos_[idx].compare_exchange_strong(pos, 0);
    assert(success);
    if (new_pos > 0)
    {
        assert((new_pos & consuming_flag) == 0);
        if (pos_[other_idx].compare_exchange_strong(new_pos, new_pos | consuming_flag))
        {
            return std::span<const uint8_t>{buffer_of(other_idx), new_pos};
        }
        assert(false);
    }
    return std::nullopt;
}

serial_rx_buffer::size_type serial_rx_buffer::read(uint8_t* data, size_type length)
{
    auto consume_pos = consume_pos_.load();
    size_type consume_idx = consume_pos >= size() ? 1 : 0;
    auto produce_pos = produce_pos_[consume_idx].load();
    if (produce_pos == 0)
    {
        consume_idx = size_type{1} - consume_idx;
        produce_pos = produce_pos_[consume_idx].load();
        if (produce_pos == 0)
        {
            return 0;
        }
        consume_pos = consume_idx * size();
    }

    size_type readable = std::min(produce_pos - consume_pos, length);
    auto readable_data = std::span<uint8_t>{buffer(), size() * 2}.subspan(consume_pos, readable);
    std::ranges::copy(readable_data, data);
    consume_pos += readable;
    if (consume_pos == produce_pos)
    {
        consume_pos = (size_type{1} - consume_idx) * size();
        produce_pos_[consume_idx].store(0);
    }
    consume_pos_.store(consume_pos);
    return readable;
}

std::span<uint8_t> serial_rx_buffer::empty_side()
{
    for (size_type i = 0; i < 2; ++i)
    {
        if (produce_pos_[i].load() == 0)
        {
            return {buffer_of(i), size()};
        }
    }
    return {};
}

bool serial_rx_buffer::set_produced(const std::span<uint8_t>& data)
{
    auto offset = static_cast<size_type>(data.data() - buffer());
    size_type produce_idx = offset >= size() ? 1 : 0;
    produce_pos_[produce_idx].store(offset + data.size());
    return produce_pos_[size_type{1} - produce_idx].load() == 0;
}

} // namespace usb
