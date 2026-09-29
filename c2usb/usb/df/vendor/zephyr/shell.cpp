// SPDX-License-Identifier: MPL-2.0
#include "usb/df/vendor/zephyr/shell.hpp"
#include <cassert>

extern "C" const struct shell* c2usb_shell_handle();
extern struct shell_transport c2usb_shell_transport;

namespace usb::df::zephyr
{

shell& shell::handle()
{
    // the buffers and their midpoints have to be USB transfer aligned
    static_assert(((CONFIG_C2USB_SHELL_BACKEND_TX_BUFFER_SIZE / 2) % alignof(std::uintptr_t)) == 0);
    static_assert(((CONFIG_C2USB_SHELL_BACKEND_RX_BUFFER_SIZE / 2) % alignof(std::uintptr_t)) == 0);
    static std::array<uint8_t, CONFIG_C2USB_SHELL_BACKEND_TX_BUFFER_SIZE> tx alignas(
        std::uintptr_t);
    static std::array<uint8_t, CONFIG_C2USB_SHELL_BACKEND_RX_BUFFER_SIZE> rx alignas(
        std::uintptr_t);
    static shell sh(tx, rx);
    return sh;
}

const ::shell_transport_api& shell::shell_tp_api()
{
    static const ::shell_transport_api api = {
        .init = shell_tp_init,
        .uninit = shell_tp_uninit,
        .enable = shell_tp_enable,
        .write = shell_tp_write,
        .read = shell_tp_read,
    };
    return api;
}

void shell::change_active(bool active)
{
    if (active)
    {
        receive_buffer_data();
        if (!::shell_ready(c2usb_shell_handle()))
        {
            ::shell_start(c2usb_shell_handle());
        }
    }
    else if (::shell_ready(c2usb_shell_handle()))
    {
        ::k_sem_take(&c2usb_shell_handle()->ctx->lock_sem, K_FOREVER);
        tx_buffer_.reset();
        rx_buffer_.reset();
        ::shell_stop(c2usb_shell_handle());
        ::k_sem_give(&c2usb_shell_handle()->ctx->lock_sem);
    }
}

void shell::set_line(const line_config& old_cfg, const line_config& new_cfg)
{
    if (old_cfg.data_terminal_ready() != new_cfg.data_terminal_ready())
    {
        change_active(new_cfg.data_terminal_ready());
    }
}

void shell::reset_line()
{
    change_active(false);
}

shell::shell(const std::span<uint8_t>& tx_buffer, const std::span<uint8_t>& rx_buffer)
    : function(sizeof(CONFIG_SHELL_C2USB_FUNCTION_NAME) > sizeof("")
                   ? CONFIG_SHELL_C2USB_FUNCTION_NAME
                   : nullptr),
      tx_buffer_{tx_buffer},
      rx_buffer_{rx_buffer}
{
    assert(c2usb_shell_transport.ctx == nullptr);
    c2usb_shell_transport.ctx = this;
    c2usb_shell_transport.api = &shell::shell_tp_api();

    // CONFIG_SHELL_LOG_BACKEND
    bool log_backend = CONFIG_C2USB_SHELL_BACKEND_LOG_LEVEL > 0;
    uint32_t level = (CONFIG_C2USB_SHELL_BACKEND_LOG_LEVEL > LOG_LEVEL_DBG)
                         ? CONFIG_LOG_MAX_LEVEL
                         : CONFIG_C2USB_SHELL_BACKEND_LOG_LEVEL;

    const struct ::shell_backend_config_flags cfg_flags = SHELL_DEFAULT_BACKEND_CONFIG_FLAGS;
    ::shell_init(c2usb_shell_handle(), this, cfg_flags, log_backend, level);
}

shell::~shell()
{
    ::shell_uninit(c2usb_shell_handle(), nullptr);
}

int shell::shell_tp_init(const ::shell_transport* transport, const void* config,
                         ::shell_transport_handler_t evt_handler, void* context)
{
    auto* self = static_cast<shell*>(const_cast<void*>(config));
    self->tp_handler_ = evt_handler;
    self->shell_context_ = context;
    return 0;
}

int shell::shell_tp_uninit(const ::shell_transport* transport)
{
    return 0;
}

int shell::shell_tp_enable(const ::shell_transport* transport, bool blocking_tx)
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

int shell::shell_tp_write(const ::shell_transport* transport, const void* data, size_t length,
                          size_t* cnt)
{
    auto* self = static_cast<shell*>(transport->ctx);
    assert(length);
    *cnt = length;

    if (not self->get_line_config().data_terminal_ready())
    {
        // drop data while the pipe is not ready
        self->tx_done_handler();
    }
    else if (auto to_consume = self->tx_buffer_.write(static_cast<const uint8_t*>(data), cnt);
             to_consume)
    {
        auto result = self->send_data(to_consume.value());
        if (result != usb::result::ok)
        {
            self->tx_buffer_.cancel_consume(to_consume.value());
        }
    }
    return 0;
}

void shell::data_sent(const std::span<const uint8_t>& tx, bool needs_zlp)
{
    auto next = tx_buffer_.advance(tx, needs_zlp);
    if (not next.has_value() or not next->empty())
    {
        tx_done_handler();
    }
    else
    {
        // next is ZLP, no buffer space freed
    }

    if (next.has_value())
    {
        auto result = send_data(next.value());
        if (result != usb::result::ok)
        {
            tx_buffer_.cancel_consume(next.value());
        }
    }
}

int shell::shell_tp_read(const struct shell_transport* transport, void* data, size_t length,
                         size_t* cnt)
{
    // read some data from the buffer (character-by-character)
    auto* self = static_cast<shell*>(transport->ctx);
    *cnt = self->rx_buffer_.read(static_cast<uint8_t*>(data), length);
    self->receive_buffer_data();
    return 0;
}

void shell::receive_buffer_data()
{
    auto read_to = rx_buffer_.empty_side();
    if (read_to.size())
    {
        receive_data(read_to);
    }
}

void shell::data_received(const std::span<uint8_t>& rx)
{
    bool ready_to_rx = rx_buffer_.set_produced(rx);
    if (ready_to_rx)
    {
        receive_buffer_data();
    }
    rx_ready_handler();
}

} // namespace usb::df::zephyr
