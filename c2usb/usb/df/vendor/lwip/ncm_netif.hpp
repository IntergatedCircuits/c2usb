// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "usb/df/class/cdc_ncm.hpp"
#include <freertos/thread.hpp>
#include <lwip/netif.h>

namespace usb::df::lwip
{
class ncm_netif : public cdc::ncm::network_interface
{
  public:
    /// @brief  Constructs an NCM network interface
    /// @param  local_address: the MAC address of the network interface
    /// @param  worker_thread: The thread which will call the worker() method to handle network
    ///         events
    explicit ncm_netif(const usb::cdc::mac_address& local_address, freertos::thread* worker_thread);

    ::netif* netif_handle() { return &netif_; }

    /// @brief  The network interface initialization function to be passed to lwip.
    static err_t init(::netif* netif);

    /// @brief  The main worker loop handling network events.
    /// @note   This method must be called from the thread specified in the constructor.
    void worker();

    /// @brief  Stops the worker loop by notifying it to terminate.
    void stop_worker() { notify_worker(TERMINATE_FLAG); }

  private:
    static constexpr freertos::thread::notify_value STATE_CHANGE_FLAG = 1;
    static constexpr freertos::thread::notify_value DATA_RECEIVED_FLAG = 2;
    static constexpr freertos::thread::notify_value TERMINATE_FLAG = 4;

    void notify_worker(freertos::thread::notify_value value) { notifier_.set_flags(value); }

    static err_t link_output(::netif* netif, ::pbuf* packet);

    void state_change(cdc::ncm::function& function, cdc::ncm::state old_state,
                      cdc::ncm::state new_state) override;
    void data_received(cdc::ncm::function& function) override;
    void tx_buffer_available(cdc::ncm::function& function) override;

    ::netif netif_{};
    std::atomic<cdc::ncm::function*> function_{};
    std::atomic<freertos::thread*> tx_thread_{};
    std::span<const uint8_t> rx_dg_{};
    freertos::thread::notifier notifier_;
};

} // namespace usb::df::lwip
