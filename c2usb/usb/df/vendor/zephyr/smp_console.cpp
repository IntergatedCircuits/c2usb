// SPDX-License-Identifier: MPL-2.0
#include <algorithm>
#include <cassert>
#include "usb/df/vendor/zephyr/smp_console.hpp"
#include <mgmt/mcumgr/transport/smp_internal.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>

namespace usb::df::zephyr
{
smp_console& smp_console::handle()
{
    static_assert(
        ((CONFIG_C2USB_MCUMGR_SMP_CONSOLE_TX_BUFFER_SIZE / 2) % alignof(std::uintptr_t)) == 0);
    static_assert(
        ((CONFIG_C2USB_MCUMGR_SMP_CONSOLE_RX_BUFFER_SIZE / 2) % alignof(std::uintptr_t)) == 0);
    static std::array<uint8_t, CONFIG_C2USB_MCUMGR_SMP_CONSOLE_TX_BUFFER_SIZE> tx alignas(
        std::uintptr_t);
    static std::array<uint8_t, CONFIG_C2USB_MCUMGR_SMP_CONSOLE_RX_BUFFER_SIZE> rx alignas(
        std::uintptr_t);
    static smp_console transport(tx, rx);
    return transport;
}

smp_console::smp_console(const std::span<uint8_t>& tx_buffer, const std::span<uint8_t>& rx_buffer)
    : function(CONFIG_C2USB_MCUMGR_SMP_CONSOLE_FUNCTION_NAME[0]
                   ? CONFIG_C2USB_MCUMGR_SMP_CONSOLE_FUNCTION_NAME
                   : nullptr),
      tx_buffer_{tx_buffer},
      rx_buffer_{rx_buffer}
{
    smp_.functions.output = smp_tx_packet;
    smp_.functions.get_mtu = smp_get_mtu;
    [[maybe_unused]] int ret = ::smp_transport_init(&smp_);
    assert(ret == 0);
#ifdef CONFIG_SMP_CLIENT
    smp_client_transport_.smpt = &smp_;
    smp_client_transport_.smpt_type = SMP_SERIAL_TRANSPORT;
    ::smp_client_transport_register(&smp_client_transport_);
#endif
}

smp_console::~smp_console()
{
    reset_rx();
}

void smp_console::set_line(const line_config& old_cfg, const line_config& new_cfg)
{
    if (old_cfg.data_terminal_ready() != new_cfg.data_terminal_ready())
    {
        set_active(new_cfg.data_terminal_ready());
    }
}

void smp_console::reset_line()
{
    set_active(false);
}

void smp_console::set_active(bool active)
{
    if (active)
    {
        tx_buffer_.reset();
        reset_rx();
        receive_data(rx_buffer_);
    }
    else
    {
        tx_signal_.release();
        tx_buffer_.reset();
        reset_rx();
    }
}

void smp_console::data_received(const std::span<uint8_t>& rx)
{
    if (not get_line_config().data_terminal_ready())
    {
        return;
    }

    // process each line delimited by '\n'
    for (size_t offset = 0; offset < rx.size();)
    {
        auto remaining = rx.subspan(offset);
        auto newline = std::find(remaining.begin(), remaining.end(), '\n');
        size_t length = newline == remaining.end()
                            ? remaining.size()
                            : static_cast<size_t>(newline - remaining.begin()) + 1;

        process_rx_range(remaining.first(length));
        offset += length;
    }

    receive_data(rx_buffer_);
}

void smp_console::process_rx_range(const std::span<uint8_t>& rx)
{
    bool delimited = rx.back() == '\n';

    // oversized fragment, discard it
    if ((rx.size() + smp_rx_fragment_size_) > smp_rx_fragment_.size())
    {
        smp_rx_fragment_size_ = smp_rx_fragment_.size();
        if (delimited)
        {
            reset_rx();
        }
        return;
    }

    // append the received fragment to the buffer
    std::copy(rx.begin(), rx.end(), smp_rx_fragment_.begin() + smp_rx_fragment_size_);
    smp_rx_fragment_size_ += rx.size();

    if (delimited)
    {
        // process the complete fragment
        if (auto* packet = ::mcumgr_serial_process_frag(&smp_rx_ctxt_, smp_rx_fragment_.data(),
                                                        smp_rx_fragment_size_);
            packet != nullptr)
        {
            ::smp_rx_req(&smp_, packet);
        }
        smp_rx_fragment_size_ = 0;
    }
}

void smp_console::reset_rx()
{
    smp_rx_fragment_size_ = 0;
    if (smp_rx_ctxt_.nb != nullptr)
    {
        smp_packet_free(smp_rx_ctxt_.nb);
        smp_rx_ctxt_.nb = nullptr;
    }
    smp_rx_ctxt_.pkt_len = 0;
}

void smp_console::data_sent(const std::span<const uint8_t>& tx, bool needs_zlp)
{
    auto next = tx_buffer_.advance(tx, needs_zlp);
    if (not next.has_value() or not next->empty())
    {
        // buffer space got available, unless ZLP is next
        tx_signal_.release();
    }

    if (next.has_value())
    {
        if (auto result = send_data(next.value()); result != usb::result::ok)
        {
            tx_buffer_.cancel_consume(next.value());
        }
    }
}

int smp_console::smp_tx_raw(const void* data, int len)
{
    auto* self = &handle();
    assert(len > 0);
    while (len > 0)
    {
        if (not self->get_line_config().data_terminal_ready())
        {
            return -ENOTCONN;
        }
        size_t count = static_cast<size_t>(len);
        if (auto to_consume =
                self->tx_buffer_.write(reinterpret_cast<const uint8_t*>(data), &count);
            to_consume)
        {
            if (auto result = self->send_data(to_consume.value()); result != usb::result::ok)
            {
                self->tx_buffer_.cancel_consume(to_consume.value());
                return result.to_int();
            }
        }
        if (count > 0)
        {
            data = static_cast<const uint8_t*>(data) + count;
            len -= static_cast<int>(count);
        }
        else
        {
            self->tx_signal_.acquire();
        }
    }
    return 0;
}

int smp_console::smp_tx_packet(::net_buf* buf)
{
    int ret = -ENOTCONN;

    if (auto* self = &handle(); self->get_line_config().data_terminal_ready())
    {
        self->tx_signal_.try_acquire();
        ret = mcumgr_serial_tx_pkt(buf->data, buf->len, smp_tx_raw);
    }

    smp_packet_free(buf);
    return ret;
}

uint16_t smp_console::smp_get_mtu([[maybe_unused]] const ::net_buf* buf)
{
    return CONFIG_C2USB_MCUMGR_SMP_CONSOLE_MTU;
}

} // namespace usb::df::zephyr
