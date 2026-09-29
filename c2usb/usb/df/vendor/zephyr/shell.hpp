// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <optional>
#include "usb/df/class/cdc_acm.hpp"
#include "usb/serial_buffer.hpp"
#include <zephyr/shell/shell.h>
#if CONFIG_C2USB_SHELL_MCUMGR_TRANSPORT
#include <zephyr/mgmt/mcumgr/transport/serial.h>
#include <zephyr/mgmt/mcumgr/transport/smp.h>
#endif

namespace usb::df::zephyr
{
class shell : public usb::df::cdc::acm::function
{
  public:
#if CONFIG_C2USB_SHELL_BACKEND
    static shell& handle();
#endif

  private:
    shell(const std::span<uint8_t>& tx_buffer, const std::span<uint8_t>& rx_buffer);
    ~shell() override;

    void set_line(const line_config& old_cfg, const line_config& new_cfg) override;
    void reset_line() override;
    void data_sent(const std::span<const uint8_t>& tx, bool needs_zlp) override;
    void data_received(const std::span<uint8_t>& rx) override;

    void receive_buffer_data();

    static int shell_tp_init(const ::shell_transport* transport, const void* config,
                             ::shell_transport_handler_t evt_handler, void* context);
    static int shell_tp_uninit(const ::shell_transport* transport);
    static int shell_tp_enable(const ::shell_transport* transport, bool blocking_tx);
    static int shell_tp_write(const ::shell_transport* transport, const void* data, size_t length,
                              size_t* cnt);
    static int shell_tp_read(const ::shell_transport* transport, void* data, size_t length,
                             size_t* cnt);
    void change_active(bool active);
    static const ::shell_transport_api& shell_tp_api();

    void tp_handler(::shell_transport_evt evt)
    {
        if (tp_handler_)
        {
            tp_handler_(evt, shell_context_);
        }
    }
    void tx_done_handler() { tp_handler(::shell_transport_evt::SHELL_TRANSPORT_EVT_TX_RDY); }
    void rx_ready_handler() { tp_handler(::shell_transport_evt::SHELL_TRANSPORT_EVT_RX_RDY); }

    ::shell_transport_handler_t tp_handler_{};
    void* shell_context_{};
    serial_tx_buffer tx_buffer_;
#if CONFIG_C2USB_SHELL_MCUMGR_TRANSPORT
    class rx_buffer : public serial_rx_buffer
    {
      public:
        using serial_rx_buffer::read;
        using serial_rx_buffer::serial_rx_buffer;

      private:
        std::array<uint8_t, 2> restored_bytes_{};

      public:
        size_type read(uint8_t* data, size_type length, bool restore);
        bool set_produced(const std::span<uint8_t>& data, uint8_t restored_byte);
    } rx_buffer_;
    ::smp_transport smp_;
    ::mcumgr_serial_rx_ctxt smp_rx_ctxt_{};
    std::array<uint8_t, MCUMGR_SERIAL_MAX_FRAME> smp_rx_fragment_{};
    uint8_t smp_rx_restore_byte_{};
    size_t smp_rx_fragment_size_{};

    static int smp_tx_packet(::net_buf* buf);
    static uint16_t smp_get_mtu(const ::net_buf* buf);
    static int smp_tx_raw(const void* data, int len);
    std::optional<uint8_t> consume_smp_data(std::span<uint8_t>& rx);

#else
    serial_rx_buffer rx_buffer_;
#endif // CONFIG_C2USB_SHELL_MCUMGR_TRANSPORT
};

} // namespace usb::df::zephyr
