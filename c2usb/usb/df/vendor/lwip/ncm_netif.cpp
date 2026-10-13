// SPDX-License-Identifier: MPL-2.0
#include "usb/df/vendor/lwip/ncm_netif.hpp"
#include <lwip/debug.h>
#include <lwip/etharp.h>
#include <lwip/netifapi.h>
#include <netif/ethernet.h>

using namespace freertos;
using namespace std::chrono_literals;

namespace usb::df::lwip
{
ncm_netif::ncm_netif(const usb::cdc::mac_address& local_address, freertos::thread* worker_thread)
    : notifier_{*worker_thread}
{
    netif_.hwaddr_len = sizeof(local_address);
    std::memcpy(netif_.hwaddr, local_address.data(), sizeof(local_address));
}

err_t ncm_netif::init(::netif* netif)
{
    assert(&static_cast<ncm_netif*>(netif->state)->netif_ == netif);

    netif->name[0] = 'u';
    netif->name[1] = 'n';
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST;
#if LWIP_IPV4 && LWIP_ARP
    netif->flags |= NETIF_FLAG_ETHARP;
    netif->output = etharp_output;
#endif
#if LWIP_IPV6
    netif->output_ip6 = ethip6_output;
#if LWIP_IPV6_MLD
    netif->flags |= NETIF_FLAG_MLD6;
#endif
#endif
    netif->linkoutput = link_output;

    // notify the network interface of status changes,
    // so the NCM connection state can follow it
#if LWIP_NETIF_STATUS_CALLBACK
    netif_set_status_callback(
        netif, [](::netif* netif)
        { static_cast<ncm_netif*>(netif->state)->notify_worker(STATE_CHANGE_FLAG); });
#endif

    return ERR_OK;
}

err_t ncm_netif::link_output(::netif* netif, ::pbuf* packet)
{
    auto& self = *static_cast<ncm_netif*>(netif->state);
    auto* function = self.function_.load();
    if (!function)
    {
        return ERR_RTE;
    }

    // only one thread is allowed to transmit at a time, ensured by lwip core lock
    [[maybe_unused]] auto other = self.tx_thread_.exchange(thread::get_current());
    assert(other == nullptr);

    std::span<uint8_t> datagram;
    auto result = function->allocate_datagram(packet->tot_len, datagram);
    if (result == std::errc::resource_unavailable_try_again)
    {
        // wait for the TX buffer to become available
        this_thread::wait_signal();
        result = function->allocate_datagram(packet->tot_len, datagram);
    }
    self.tx_thread_.store(nullptr);

    if (result == std::errc::not_connected)
    {
        return ERR_RTE;
    }
    if (result != c2usb::result::ok)
    {
        return ERR_IF;
    }

    // when allocated, insert the packet data into the datagram
    if (pbuf_copy_partial(packet, datagram.data(), packet->tot_len, 0) != packet->tot_len)
    {
        return ERR_BUF;
    }
    // commit the datagram for transmission
    return function->commit_datagram(datagram) == c2usb::result::ok ? ERR_OK : ERR_IF;
}

void ncm_netif::worker()
{
    // direct to task notifications are used, the thread must match what's being notified
    assert(thread::get_current() == notifier_.get_thread());

    thread::notify_value event_flags{};
    tick_timer::duration timeout{freertos::infinity};
    while (true)
    {
        this_thread::wait_notification_for(timeout, &event_flags, 0, ~0);
        if (event_flags & TERMINATE_FLAG)
        {
            return;
        }
        // other events are sent by the function callbacks, so function must be valid
        auto& function = *function_.load();
        assert(&function != nullptr);

        // when all goes well, only need to wait for the next event
        timeout = freertos::infinity;

        if (event_flags & STATE_CHANGE_FLAG)
        {
            event_flags &= ~STATE_CHANGE_FLAG;

            switch (function.get_state())
            {
            case cdc::ncm::state::CONNECTED:
                netifapi_netif_set_link_up(&netif_);
                if (netif_is_up(&netif_))
                {
                    break;
                }
                if (auto result = function.disconnect(); result != c2usb::result::ok)
                {
                    LWIP_DEBUGF(NETIF_DEBUG,
                                ("ncm_netif: Failed to disconnect: %d\n", result.to_int()));
                    timeout = 50ms;
                    event_flags |= STATE_CHANGE_FLAG;
                }
                break;
            case cdc::ncm::state::ENABLED:
                netifapi_netif_set_link_down(&netif_);
                if (not netif_is_up(&netif_))
                {
                    break;
                }
                if (auto result = function.connect(); result != c2usb::result::ok)
                {
                    LWIP_DEBUGF(NETIF_DEBUG,
                                ("ncm_netif: Failed to connect: %d\n", result.to_int()));
                    timeout = 50ms;
                    event_flags |= STATE_CHANGE_FLAG;
                }
                break;
            case cdc::ncm::state::DISABLED:
            default:
                netifapi_netif_set_link_down(&netif_);
                break;
            }
        }
        if (event_flags & DATA_RECEIVED_FLAG)
        {
            event_flags &= ~DATA_RECEIVED_FLAG;

            // the last attempted datagram is stored and reused
            if (rx_dg_.empty())
            {
                rx_dg_ = function.pop_datagram();
            }
            for (; !rx_dg_.empty(); rx_dg_ = function.pop_datagram())
            {
                // allocate packet, insert data and pass it to the network interface
                auto* packet = pbuf_alloc(PBUF_RAW, rx_dg_.size(), PBUF_POOL);
                if ((packet == nullptr) or
                    (pbuf_take(packet, rx_dg_.data(), rx_dg_.size()) != ERR_OK) or
                    (netif_.input(packet, &netif_) != ERR_OK))
                {
                    LWIP_DEBUGF(NETIF_DEBUG, ("ncm_netif: Failed to pass rx datagram\n"));
                    if (packet)
                    {
                        pbuf_free(packet);
                    }
                    // retry after a short delay
                    timeout = 10ms;
                    event_flags |= DATA_RECEIVED_FLAG;
                    break;
                }
            }
        }
    }
}

void ncm_netif::state_change(cdc::ncm::function& function,
                             [[maybe_unused]] cdc::ncm::state old_state,
                             [[maybe_unused]] cdc::ncm::state new_state)
{
    // this will always be the first callback, store the function pointer
    function_.store(&function);
    notify_worker(STATE_CHANGE_FLAG);

    // unblock waiting TX thread
    tx_buffer_available(function);
}

void ncm_netif::data_received([[maybe_unused]] cdc::ncm::function& function)
{
    notify_worker(DATA_RECEIVED_FLAG);
}

void ncm_netif::tx_buffer_available([[maybe_unused]] cdc::ncm::function& function)
{
    if (auto* tx_thread = tx_thread_.load(); tx_thread != nullptr)
    {
        thread::notifier notifier{*tx_thread};
        notifier.signal();
    }
}

} // namespace usb::df::lwip
