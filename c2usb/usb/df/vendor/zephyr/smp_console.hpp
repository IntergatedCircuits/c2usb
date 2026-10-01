// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <span>
#include "usb/df/class/cdc_acm.hpp"
#include "usb/serial_buffer.hpp"
#include <zephyr/mgmt/mcumgr/transport/serial.h>
#include <zephyr/mgmt/mcumgr/transport/smp.h>
#include <zephyr/semaphore.hpp>

namespace usb::df::zephyr
{
class smp_console : public usb::df::cdc::acm::function
{
  public:
#if CONFIG_C2USB_MCUMGR_SMP_CONSOLE
    static smp_console& handle();
#endif

  private:
    smp_console(const std::span<uint8_t>& tx_buffer, const std::span<uint8_t>& rx_buffer);
    ~smp_console() override;

    void set_line(const line_config& old_cfg, const line_config& new_cfg) override;
    void reset_line() override;
    void data_sent(const std::span<const uint8_t>& tx, bool needs_zlp) override;
    void data_received(const std::span<uint8_t>& rx) override;
    void set_active(bool active);
    void process_rx_range(const std::span<uint8_t>& rx);
    void reset_rx();

    static int smp_tx_packet(::net_buf* buf);
    static uint16_t smp_get_mtu(const ::net_buf* buf);
    static int smp_tx_raw(const void* data, int len);

    serial_tx_buffer tx_buffer_;
    std::span<uint8_t> rx_buffer_;
    ::smp_transport smp_{};
    ::mcumgr_serial_rx_ctxt smp_rx_ctxt_{};
#ifdef CONFIG_SMP_CLIENT
    ::smp_client_transport_entry smp_client_transport_{};
#endif
    std::array<uint8_t, MCUMGR_SERIAL_MAX_FRAME> smp_rx_fragment_{};
    size_t smp_rx_fragment_size_{};
    ::zephyr::binary_semaphore tx_signal_{0};
};

} // namespace usb::df::zephyr
