// SPDX-License-Identifier: MPL-2.0
#include "usb/df/vendor/zephyr/cdc_ncm_ethernet.hpp"
#include <magic_enum.hpp>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_pkt.h>

LOG_MODULE_REGISTER(cdc_ncm_eth, CONFIG_C2USB_CDC_NCM_ETHERNET_LOG_LEVEL);

extern "C" int c2usb_cdc_ncm_ethernet_init(const ::device* dev)
{
    new (dev->data) usb::df::zephyr::cdc_ncm_ethernet(dev);
    return 0;
}

extern "C" void c2usb_cdc_ncm_ethernet_get_mac(const ::device* dev, uint8_t* mac_address)
    __attribute__((weak));

namespace usb::df::zephyr
{
using namespace std::chrono_literals;

cdc_ncm_ethernet::cdc_ncm_ethernet(const ::device* dev)
    : ::c2usb_cdc_ncm_ethernet{}
{
    config_ = static_cast<const ::c2usb_cdc_ncm_ethernet_config*>(dev->config);
    ::k_work_init_delayable(&works_, &perform_work_handler);
    ::k_event_init(&events_);
}

cdc_ncm_ethernet* cdc_ncm_ethernet::from_device(const ::device* dev)
{
    if ((dev == nullptr) or (dev->data == nullptr) or not device_is_ready(dev))
    {
        return nullptr;
    }
    return static_cast<cdc_ncm_ethernet*>(const_cast<void*>(dev->data));
}

void cdc_ncm_ethernet::destruct(cdc::ncm::function& function)
{
    auto& self = static_cast<cdc_ncm_ethernet&>(function.cleanup());
    ::net_if_carrier_off(self.netif_);
    self.function_ = nullptr;
    function.~function();
}

void cdc_ncm_ethernet::driver_iface_init(::net_if* netif)
{
    auto& self = by_device(::net_if_get_device(netif));
    self.netif_ = netif;

    // retrieve the MAC address from the weakly defined function, if available
    if (auto get_mac = &c2usb_cdc_ncm_ethernet_get_mac)
    {
        get_mac(::net_if_get_device(netif), self.mac_address);
    }
    if (::sys_get_le48(self.mac_address) == 0)
    {
        // generate random MAC address (locally administered, unicast (LAA))
        self.mac_address[0] |= 0x02;
        ::sys_rand_get(&self.mac_address[3], 3);
    }

    ::ethernet_init(netif);
    ::net_if_set_link_addr(netif, self.mac_address, sizeof(self.mac_address), NET_LINK_ETHERNET);
    ::net_if_carrier_off(netif);

    LOG_DBG("network interface initialized");
}

void cdc_ncm_ethernet::state_change(cdc::ncm::function& function, cdc::ncm::state old_state,
                                    cdc::ncm::state new_state)
{
    LOG_DBG("USB interface %s", magic_enum::enum_name(new_state).data());
    events_handle().set(NEW_STATE_FLAG);
    if (new_state == cdc::ncm::state::DISABLED)
    {
        ::net_if_carrier_off(netif_);
        work_handle().cancel();
    }
    else if (::net_if_is_admin_up(netif_) != (new_state == cdc::ncm::state::CONNECTED))
    {
        work_handle().reschedule(10ms);
    }
}

int cdc_ncm_ethernet::driver_start(const ::device* dev)
{
    auto& self = by_device(dev);
    LOG_DBG("network interface started");
    if ((self.function_ != nullptr) and (self.function_->get_state() == cdc::ncm::state::ENABLED))
    {
        self.work_handle().reschedule(0ms);
    }
    return 0;
}

int cdc_ncm_ethernet::driver_stop(const ::device* dev)
{
    auto& self = by_device(dev);
    LOG_DBG("network interface stopped");
    if ((self.function_ != nullptr) and (self.function_->get_state() == cdc::ncm::state::CONNECTED))
    {
        self.work_handle().reschedule(0ms);
    }
    return 0;
}

void cdc_ncm_ethernet::perform_work()
{
    if (function_ == nullptr)
    {
        return;
    }

    // indicate connection state to network stack
    auto state = function_->get_state();
    bool connected = state == cdc::ncm::state::CONNECTED;
    if (connected)
    {
        ::net_if_carrier_on(netif_);
    }
    else
    {
        ::net_if_carrier_off(netif_);
    }
    // try to synchronize connection state to network
    if (::net_if_is_admin_up(netif_) != connected)
    {
        c2usb::result result = c2usb::result::ok;
        switch (state)
        {
        case cdc::ncm::state::ENABLED:
            result = function_->connect();
            break;
        case cdc::ncm::state::CONNECTED:
            result = function_->disconnect();
            break;
        default:
            break;
        }
        // the notification must be delivered, retry if it fails
        if (result != c2usb::result::ok)
        {
            LOG_ERR("Failed to update link state: %d", result.to_int());
            work_handle().reschedule(50ms);
        }
    }

    // consume received datagrams:
    // the last attempted datagram is stored and reused
    auto& rx_dg = rx_datagram();
    if (rx_dg.empty())
    {
        rx_dg = function_->pop_datagram();
    }
    for (; not rx_dg.empty(); rx_dg = function_->pop_datagram())
    {
        // allocate packet, insert data and pass it to the network interface
        auto* packet = ::net_pkt_rx_alloc_with_buffer(netif_, rx_dg.size(), NET_AF_UNSPEC,
                                                      NET_IPPROTO_IP, K_MSEC(10));
        if ((packet == nullptr) or (::net_pkt_write(packet, rx_dg.data(), rx_dg.size()) != 0) or
            (::net_recv_data(netif_, packet) < 0))
        {
            LOG_ERR("Failed to pass rx datagram");
            if (packet)
            {
                ::net_pkt_unref(packet);
            }
            // retry after a short delay
            work_handle().reschedule(10ms);
            break;
        }
    }
}

void cdc_ncm_ethernet::data_received([[maybe_unused]] cdc::ncm::function& function)
{
    work_handle().reschedule(0ms);
}

int cdc_ncm_ethernet::driver_send(const ::device* dev, ::net_pkt* packet)
{
    auto& self = by_device(dev);
    if (self.function_ == nullptr)
    {
        LOG_ERR("Failed to send packet: function not initialized");
        return -ENETDOWN;
    }
    auto& function = *self.function_;

    self.events_handle().clear(TX_DONE_FLAG | NEW_STATE_FLAG);

    const auto length = ::net_pkt_get_len(packet);
    std::span<uint8_t> datagram;

    auto result = function.allocate_datagram(length, datagram);
    if (result == std::errc::resource_unavailable_try_again)
    {
        // block until TX completes, or host disables the function
        self.events_handle().wait_any(TX_DONE_FLAG | NEW_STATE_FLAG);
        result = function.allocate_datagram(length, datagram);
    }
    if (result != c2usb::result::ok)
    {
        LOG_ERR("Failed to allocate TX datagram: %d", result.to_int());
        return result.to_int();
    }

    ::net_pkt_cursor_init(packet);
    if (auto ret = ::net_pkt_read(packet, datagram.data(), datagram.size()); ret != 0)
    {
        return ret;
    }

    result = function.commit_datagram(datagram);
    if (result != c2usb::result::ok)
    {
        LOG_ERR("Failed to commit TX datagram");
    }
    return result.to_int();
}

void cdc_ncm_ethernet::tx_buffer_available([[maybe_unused]] cdc::ncm::function& function)
{
    events_handle().set(TX_DONE_FLAG);
}

::ethernet_hw_caps cdc_ncm_ethernet::driver_capabilities([[maybe_unused]] const ::device*)
{
    return ::ethernet_hw_caps(int(::ethernet_hw_caps::ETHERNET_LINK_10BASE)
#if CONFIG_C2USB_CDC_NCM_ETHERNET_PROMISC_MODE
                              | int(::ethernet_hw_caps::ETHERNET_PROMISC_MODE)
#endif
    );
}

int cdc_ncm_ethernet::driver_set_config(const ::device* dev, ::ethernet_config_type type,
                                        const ::ethernet_config* config)
{
    auto& self = by_device(dev);
    switch (type)
    {
    case ::ethernet_config_type::ETHERNET_CONFIG_TYPE_MAC_ADDRESS:
        if (self.netif_ != nullptr)
        {
            net_if_set_link_addr(self.netif_, config->mac_address.addr,
                                 sizeof(config->mac_address.addr), NET_LINK_ETHERNET);
        }
        else
        {
            LOG_WRN("Network interface not initialized, cannot set MAC address");
        }
        return 0;
#if CONFIG_C2USB_CDC_NCM_ETHERNET_PROMISC_MODE
    case ::ethernet_config_type::ETHERNET_CONFIG_TYPE_PROMISC_MODE:
        return 0;
#endif
    default:
        return -ENOTSUP;
    }
    return 0;
}

} // namespace usb::df::zephyr

using namespace usb::df::zephyr;

extern const ::ethernet_api c2usb_cdc_ncm_ethernet_api = {
    .iface_api = {.init = cdc_ncm_ethernet::driver_iface_init},
    .start = cdc_ncm_ethernet::driver_start,
    .stop = cdc_ncm_ethernet::driver_stop,
    .get_capabilities = cdc_ncm_ethernet::driver_capabilities,
    .set_config = cdc_ncm_ethernet::driver_set_config,
    .send = cdc_ncm_ethernet::driver_send,
};
