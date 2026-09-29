// SPDX-License-Identifier: MPL-2.0
#include "usb/df/vendor/zephyr/shell.hpp"
#include <algorithm>
#include <cassert>
#if CONFIG_C2USB_SHELL_MCUMGR_TRANSPORT
#include <mgmt/mcumgr/transport/smp_internal.h>
#include <zephyr/net_buf.h>
#endif

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

#if CONFIG_C2USB_SHELL_MCUMGR_TRANSPORT
    smp_.functions.output = &smp_tx_packet;
    smp_.functions.get_mtu = &smp_get_mtu;

    [[maybe_unused]] auto ret = smp_transport_init(&smp_);
#endif
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
#ifdef CONFIG_C2USB_SHELL_MCUMGR_TRANSPORT
    *cnt = self->rx_buffer_.read(static_cast<uint8_t*>(data), length, true);
#else
    *cnt = self->rx_buffer_.read(static_cast<uint8_t*>(data), length);
#endif
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
#if CONFIG_C2USB_SHELL_MCUMGR_TRANSPORT
    auto shell_data = rx;
    auto restored_byte = consume_smp_data(shell_data);
    bool ready_to_rx = rx_buffer_.set_produced(shell_data, restored_byte.value_or(0));
#else
    bool ready_to_rx = rx_buffer_.set_produced(rx);
#endif
    if (ready_to_rx)
    {
        receive_buffer_data();
    }
    rx_ready_handler();
}

#if CONFIG_C2USB_SHELL_MCUMGR_TRANSPORT
bool shell::rx_buffer::set_produced(const std::span<uint8_t>& data, uint8_t restored_byte)
{
    auto produce_idx = (data.data() - buffer()) >= static_cast<std::intptr_t>(size());
    produce_pos_[produce_idx].store(data.data() - buffer() + data.size());
    restored_bytes_[produce_idx] = restored_byte;
    // return true if the other side is free
    return produce_pos_[1 - produce_idx].load() == 0;
}

serial_rx_buffer::size_type shell::rx_buffer::read(uint8_t* data, size_type length, bool restore)
{
    if (restore)
    {
        auto consume_pos = consume_pos_.load();
        auto consume_idx = consume_pos >= size();
        auto produce_pos = produce_pos_[consume_idx].load();

        // current side is empty, and restored byte is present
        if (auto& restored_byte = restored_bytes_[1 - consume_idx];
            (produce_pos == 0) and (restored_byte != 0))
        {
            *data = restored_byte;
            restored_byte = 0;
            return 1;
        }
    }
    return read(data, length);
}

std::optional<uint8_t> shell::consume_smp_data(std::span<uint8_t>& rx)
{
    std::optional<uint8_t> restored_byte{};
    size_t shell_size{};
    for (size_t pos = 0; pos < rx.size(); ++pos)
    {
        const uint8_t byte = rx[pos];
        switch (smp_rx_fragment_size_)
        {
        case 0:
            // looking for first SMP header byte
            if ((byte == MCUMGR_SERIAL_HDR_PKT_1) or (byte == MCUMGR_SERIAL_HDR_FRAG_1))
                [[unlikely]]
            {
                smp_rx_fragment_[0] = byte;
                smp_rx_fragment_size_ = 1;
            }
            else
            {
                rx[shell_size++] = byte;
            }
            break;

        case 1:
            // use first two bytes to check for header
            smp_rx_fragment_[1] = byte;
            if (auto header = *std_layout_cast<be_uint16_t*>(smp_rx_fragment_.data());
                (header == MCUMGR_SERIAL_HDR_PKT) or (header == MCUMGR_SERIAL_HDR_FRAG))
            {
                smp_rx_fragment_size_ = 2;
            }
            else
            {
                if (pos == 0)
                {
                    // if the first header byte is from the previous RX transfer,
                    // it has to be restored out of buffer
                    restored_byte = smp_rx_fragment_[0];
                }
                else
                {
                    rx[shell_size++] = smp_rx_fragment_[0];
                }
                rx[shell_size++] = byte;
                smp_rx_fragment_size_ = 0;
            }
            break;

        case MCUMGR_SERIAL_MAX_FRAME:
            // oversized fragment, discard until newline
            if (byte == '\n')
            {
                if (smp_rx_ctxt_.nb != nullptr)
                {
                    smp_packet_free(smp_rx_ctxt_.nb);
                    smp_rx_ctxt_.nb = nullptr;
                }
                smp_rx_ctxt_.pkt_len = 0;
                smp_rx_fragment_size_ = 0;
            }
            break;

        default:
            // fragment in progress, continue accumulating
            smp_rx_fragment_[smp_rx_fragment_size_++] = byte;
            if (byte == '\n')
            {
                // payload delimiter, process the fragment and reset state
                auto* nb = mcumgr_serial_process_frag(&smp_rx_ctxt_, smp_rx_fragment_.data(),
                                                      smp_rx_fragment_size_);
                if (nb != nullptr)
                {
                    smp_rx_req(&smp_, nb);
                }
                smp_rx_fragment_size_ = 0;
            }
            break;
        }
    }

    rx = rx.first(shell_size);
    return restored_byte;
}

uint16_t shell::smp_get_mtu(const ::net_buf* buf)
{
    return CONFIG_C2USB_SHELL_MCUMGR_TRANSPORT_MTU;
}

int shell::smp_tx_packet(::net_buf* buf)
{
    const auto* sh = c2usb_shell_handle();
    ::k_sem_take(&sh->ctx->lock_sem, K_FOREVER);
    int ret = mcumgr_serial_tx_pkt(buf->data, buf->len, &smp_tx_raw);
    ::k_sem_give(&sh->ctx->lock_sem);
    smp_packet_free(buf);
    return ret;
}

int shell::smp_tx_raw(const void* data, int len)
{
    const auto* sh = c2usb_shell_handle();
    if (len < 0)
    {
        return -EINVAL;
    }

    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t offset{};
    const size_t length = static_cast<size_t>(len);
    while (offset < length)
    {
        size_t count{};
        int err = shell_tp_write(&::c2usb_shell_transport, bytes + offset, length - offset, &count);
        if (err != 0)
        {
            return err;
        }
        if (count > length - offset)
        {
            return -EIO;
        }
        if (count == 0)
        {
            ::k_event_wait(&sh->ctx->signal_event, SHELL_SIGNAL_TXDONE, false, K_FOREVER);
            ::k_event_clear(&sh->ctx->signal_event, SHELL_SIGNAL_TXDONE);
        }
        else
        {
            offset += count;
        }
    }
    return 0;
}
#endif

} // namespace usb::df::zephyr
