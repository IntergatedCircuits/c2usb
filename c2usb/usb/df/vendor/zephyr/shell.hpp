// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <optional>
#include "usb/df/class/cdc_acm.hpp"
#include <zephyr/shell/shell.h>

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
    serial_rx_buffer rx_buffer_;
};

} // namespace usb::df::zephyr
