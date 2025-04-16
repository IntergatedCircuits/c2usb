#include "port/zephyr/usb_shell.hpp"
#if ((__has_include("zephyr/shell/shell.h")) && CONFIG_SHELL_BACKEND_C2USB)
#include <cassert>

extern "C" const struct shell* c2usb_shell_handle();
extern struct shell_transport c2usb_shell_transport;

namespace usb::zephyr
{

usb_shell& usb_shell::handle()
{
    // the buffers and their midpoints have to be USB transfer aligned
    static_assert(((CONFIG_SHELL_BACKEND_C2USB_TX_BUFFER_SIZE / 2) % alignof(std::uintptr_t)) == 0);
    static_assert(((CONFIG_SHELL_BACKEND_C2USB_RX_BUFFER_SIZE / 2) % alignof(std::uintptr_t)) == 0);
    static std::array<uint8_t, CONFIG_SHELL_BACKEND_C2USB_TX_BUFFER_SIZE> tx alignas(
        std::uintptr_t);
    static std::array<uint8_t, CONFIG_SHELL_BACKEND_C2USB_RX_BUFFER_SIZE> rx alignas(
        std::uintptr_t);
    static usb_shell shell(tx, rx);
    return shell;
}

const ::shell_transport_api& usb_shell::shell_tp_api()
{
    static const ::shell_transport_api api = {
        .init = shell_tp_init,
        .uninit = shell_tp_uninit,
        .enable = shell_tp_enable,
        .write = shell_tp_write,
        .read = shell_tp_read,
        .update = shell_tp_update,
    };
    return api;
}

void usb_shell::shell_tp_update(const ::shell_transport* transport)
{
    auto* this_ = static_cast<usb_shell*>(transport->ctx);
    // update this state in shell thread context to avoid trapped in mutex deadlock
    auto ready = this_->get_line_config().data_terminal_ready();
    if (this_->active_ != ready)
    {
        this_->active_ = ready;
        if (ready)
        {
            ::shell_start(c2usb_shell_handle());
        }
        else
        {
            ::shell_stop(c2usb_shell_handle());
        }
    }
}

void usb_shell::set_line(const line_config& cfg, line_event ev)
{
    if (ev == line_event::STATE_CHANGE)
    {
        if (cfg.data_terminal_ready())
        {
            receive_buffer_data();
        }
    }
}

void usb_shell::reset_line()
{
    ::shell_stop(c2usb_shell_handle());
    tx_buffer_.reset();
    rx_buffer_.reset();
}

usb_shell::usb_shell(const std::span<uint8_t>& tx_buffer, const std::span<uint8_t>& rx_buffer)
    : function(), tx_buffer_{tx_buffer}, rx_buffer_{rx_buffer}
{
    assert(c2usb_shell_transport.ctx == nullptr);
    c2usb_shell_transport.ctx = this;
    c2usb_shell_transport.api = &usb_shell::shell_tp_api();

    // CONFIG_SHELL_LOG_BACKEND
    bool log_backend = CONFIG_SHELL_BACKEND_C2USB_LOG_LEVEL > 0;
    uint32_t level = (CONFIG_SHELL_BACKEND_C2USB_LOG_LEVEL > LOG_LEVEL_DBG)
                         ? CONFIG_LOG_MAX_LEVEL
                         : CONFIG_SHELL_BACKEND_C2USB_LOG_LEVEL;

    const struct ::shell_backend_config_flags cfg_flags = SHELL_DEFAULT_BACKEND_CONFIG_FLAGS;
    ::shell_init(c2usb_shell_handle(), this, cfg_flags, log_backend, level);
}

usb_shell::~usb_shell()
{
    ::shell_uninit(c2usb_shell_handle(), nullptr);
}

int usb_shell::shell_tp_init(const ::shell_transport* transport, const void* config,
                             ::shell_transport_handler_t evt_handler, void* context)
{
    auto* this_ = static_cast<usb_shell*>(const_cast<void*>(config));
    this_->tp_handler_ = evt_handler;
    this_->shell_context_ = context;
    return 0;
}

int usb_shell::shell_tp_uninit(const ::shell_transport* transport)
{
    return 0;
}

int usb_shell::shell_tp_enable(const ::shell_transport* transport, bool blocking_tx)
{
    // shell thread start: false
    // shell log backend enable, with CONFIG_LOG_MODE_IMMEDIATE=y: true
    // log panic, with CONFIG_LOG_MODE_IMMEDIATE=n: true
    if (blocking_tx)
    {
        // there are two other threads involved in USB data transfer
        return -ENOTSUP;
    }
    return 0;
}

std::optional<std::span<const uint8_t>> acm_tx_buffer::write(const uint8_t* data, size_t* length)
{
    size_type idx{};
    size_type pos{pos_[idx].load()};
    size_type new_pos;
    size_type writable;
    do
    {
        // swap the side if this side is consuming
        while (pos & consuming_flag)
        {
            idx ^= 1;
            pos = pos_[idx].load();
        }
        new_pos = pos;

        writable = std::min(size() - pos, *length);
        if (writable == 0)
        {
            // return with 0 written bytes to trigger pend on txdone
            break;
        }
        // append new data to buffer
        std::copy(data, data + writable, &buffer_of(idx)[new_pos]);
        new_pos += writable;

        // try to update position, retry if consuming started or even finished
    } while (!pos_[idx].compare_exchange_weak(pos, new_pos));

    // data placement success, update written length
    *length = writable;

    // if new data is available, and the other buffer transfer is complete
    if ((writable > 0) and (pos_[1 - idx].load() == 0))
    {
        // try to acquire consuming flag to start sending a packet now
        if (pos_[idx].compare_exchange_strong(new_pos, new_pos | consuming_flag))
        {
            return std::span<const uint8_t>{buffer_of(idx), new_pos};
        }
    }
    return std::nullopt;
}

int usb_shell::shell_tp_write(const ::shell_transport* transport, const void* data, size_t length,
                              size_t* cnt)
{
    auto* this_ = static_cast<usb_shell*>(transport->ctx);
    assert(length);
    *cnt = length;

    if (auto to_consume = this_->tx_buffer_.write(static_cast<const uint8_t*>(data), cnt);
        to_consume)
    {
        [[maybe_unused]] auto result = this_->send_data(to_consume.value());
        assert(result != usb::result::device_or_resource_busy);
        if (result != usb::result::ok)
        {
            this_->tx_buffer_.cancel_consume(to_consume.value());
        }
    }
    return 0;
}

void acm_tx_buffer::cancel_consume(const std::span<const uint8_t>& data)
{
    bool idx = data.data() + data.size() > buffer_of(1);
    size_type pos = data.data() + data.size() - buffer_of(idx);
    pos |= consuming_flag;
    if (!pos_[idx].compare_exchange_strong(pos, pos & ~consuming_flag))
    {
        assert(false);
    }
}

std::optional<std::span<const uint8_t>> acm_tx_buffer::advance(const std::span<const uint8_t>& data,
                                                               bool needs_zlp)
{
    bool idx = data.data() + data.size() > buffer_of(1);
    size_type pos = data.data() + data.size() - buffer_of(idx);

    size_type new_pos = pos_[1 - idx].load();
    if (needs_zlp and (new_pos == 0))
    {
        // ZLP, conserving the buffer position
        return std::span<const uint8_t>{data.data() + data.size(), 0};
    }

    // clear the current buffer's position
    pos |= consuming_flag;
    [[maybe_unused]] bool success = pos_[idx].compare_exchange_strong(pos, 0);
    assert(success);

    // data available in other buffer
    if (new_pos > 0)
    {
        assert((new_pos & consuming_flag) == 0);
        // try to mark the next buffer for consuming
        if (pos_[1 - idx].compare_exchange_strong(new_pos, new_pos | consuming_flag))
        {
            return std::span<const uint8_t>{buffer_of(1 - idx), new_pos};
        }
        assert(false);
    }

    return std::nullopt;
}

void usb_shell::data_sent(const std::span<const uint8_t>& tx, bool needs_zlp)
{
    if (!tx.empty())
    {
        // data sent
        tx_done_handler();
    }
    else
    {
        // zlp
        assert(tx.data() != nullptr);
    }

    if (auto next = tx_buffer_.advance(tx, needs_zlp); next)
    {
        [[maybe_unused]] auto result = send_data(next.value());
        assert(result == usb::result::ok);
    }
}

acm_rx_buffer::size_type acm_rx_buffer::read(uint8_t* data, size_type length)
{
    auto consume_pos = consume_pos_.load();
    auto consume_idx = consume_pos >= size();
    auto produce_pos = produce_pos_[consume_idx].load();

    // current side is empty
    if (produce_pos == 0)
    {
        consume_idx = 1 - consume_idx;
        produce_pos = produce_pos_[consume_idx].load();
        // other side as well
        if (produce_pos == 0)
        {
            return 0;
        }
        consume_pos = consume_idx * size();
    }

    size_type readable = std::min(produce_pos - consume_pos, length);
    std::copy(buffer_ + consume_pos, buffer_ + consume_pos + readable, data);
    consume_pos += readable;

    if (consume_pos == produce_pos)
    {
        // this side is now empty, flip
        consume_pos = (1 - consume_idx) * size();
        produce_pos_[consume_idx].store(0);
    }
    consume_pos_.store(consume_pos);
    return readable;
}

int usb_shell::shell_tp_read(const struct shell_transport* transport, void* data, size_t length,
                             size_t* cnt)
{
    // read some data from the buffer (character-by-character)
    auto* this_ = static_cast<usb_shell*>(transport->ctx);
    *cnt = this_->rx_buffer_.read(static_cast<uint8_t*>(data), length);
    this_->receive_buffer_data();
    return 0;
}

std::span<uint8_t> acm_rx_buffer::empty_side()
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

void usb_shell::receive_buffer_data()
{
    auto read_to = rx_buffer_.empty_side();
    if (read_to.size())
    {
        receive_data(read_to);
    }
}

bool acm_rx_buffer::set_produced(const std::span<uint8_t>& data)
{
    auto produce_idx = (data.data() - buffer_) >= static_cast<std::intptr_t>(size());
    produce_pos_[produce_idx].store(data.data() - buffer_ + data.size());
    // return true if the other side is free
    return produce_pos_[1 - produce_idx].load() == 0;
}

void usb_shell::data_received(const std::span<uint8_t>& rx)
{
    if (rx_buffer_.set_produced(rx))
    {
        receive_buffer_data();
    }
    rx_ready_handler();
}

} // namespace usb::zephyr

#endif // ((__has_include("zephyr/shell/shell.h")) && CONFIG_SHELL_BACKEND_C2USB)
