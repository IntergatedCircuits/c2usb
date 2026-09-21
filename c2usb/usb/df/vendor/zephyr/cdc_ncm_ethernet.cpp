// SPDX-License-Identifier: MPL-2.0
#include "usb/df/vendor/zephyr/cdc_ncm_ethernet.hpp"
#include <zephyr/logging/log.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_pkt.h>

LOG_MODULE_REGISTER(cdc_ncm_eth, CONFIG_C2USB_CDC_NCM_ETHERNET_LOG_LEVEL);

extern "C" int c2usb_cdc_ncm_ethernet_init(const ::device* dev)
{
    new (dev->data) usb::df::zephyr::cdc_ncm_ethernet(dev);
    return 0;
}

namespace usb::df::zephyr
{
cdc_ncm_ethernet::cdc_ncm_ethernet(const ::device* dev)
    : ::c2usb_cdc_ncm_ethernet{}
{
    config_ = static_cast<const ::c2usb_cdc_ncm_ethernet_config*>(dev->config);
    ::k_work_init_delayable(&link_work_, &link_state_update_work);
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
    ::ethernet_init(netif);
    ::net_if_set_link_addr(netif, self.config_->local_mac_address,
                           sizeof(self.config_->local_mac_address), NET_LINK_ETHERNET);
    ::net_if_carrier_off(netif);

    LOG_DBG("network interface initialized");
}

void cdc_ncm_ethernet::state_change(cdc::ncm::function& function, cdc::ncm::state old_state,
                                    cdc::ncm::state new_state)
{
    ::k_event_post(&events_, NEW_STATE_FLAG);
    switch (new_state)
    {
    case cdc::ncm::state::CONNECTED:
        ::net_if_carrier_on(netif_);
        LOG_DBG("USB interface connected");
        break;
    case cdc::ncm::state::ENABLED:
        LOG_DBG("USB interface enabled");
        break;
    case cdc::ncm::state::DISABLED:
        ::net_if_carrier_off(netif_);
        ::k_work_cancel_delayable(&link_work_);
        LOG_DBG("USB interface disabled");
        return;
    default:
        return;
    }
    if (bool is_up = ::net_if_is_admin_up(netif_);
        is_up != (new_state == cdc::ncm::state::CONNECTED))
    {
        ::k_work_reschedule(&link_work_, K_MSEC(10));
    }
}

int cdc_ncm_ethernet::driver_start(const ::device* dev)
{
    auto& self = by_device(dev);
    LOG_DBG("network interface started");
    if ((self.function_ != nullptr) and (self.function_->get_state() == cdc::ncm::state::ENABLED))
    {
        ::k_work_reschedule(&self.link_work_, K_NO_WAIT);
    }
    return 0;
}

int cdc_ncm_ethernet::driver_stop(const ::device* dev)
{
    auto& self = by_device(dev);
    LOG_DBG("network interface stopped");
    if ((self.function_ != nullptr) and (self.function_->get_state() == cdc::ncm::state::CONNECTED))
    {
        ::k_work_reschedule(&self.link_work_, K_NO_WAIT);
    }
    return 0;
}

void cdc_ncm_ethernet::link_state_update()
{
    if (function_ == nullptr)
    {
        return;
    }
    auto state = function_->get_state();
    if (bool is_up = ::net_if_is_admin_up(netif_); is_up == (state == cdc::ncm::state::CONNECTED))
    {
        return;
    }
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
        return;
    }
    // the notification must be delivered, retry if it fails
    if (result != c2usb::result::ok)
    {
        LOG_ERR("Failed to update link state: %d", result.to_int());
        ::k_work_reschedule(&link_work_, K_MSEC(50));
    }
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

    ::k_event_clear(&self.events_, TX_DONE_FLAG | NEW_STATE_FLAG);

    const auto length = ::net_pkt_get_len(packet);
    std::span<uint8_t> datagram;

    auto result = function.allocate_datagram(length, datagram);
    if (result == std::errc::resource_unavailable_try_again)
    {
        // block until TX completes, or host disables the function
        ::k_event_wait(&self.events_, TX_DONE_FLAG | NEW_STATE_FLAG, false, K_FOREVER);
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

void cdc_ncm_ethernet::tx_buffer_available(cdc::ncm::function& function)
{
    ::k_event_post(&events_, TX_DONE_FLAG);
}

void cdc_ncm_ethernet::data_received([[maybe_unused]] cdc::ncm::function& function)
{
    // TODO: dispatch to different thread context?

    for (auto datagram = function_->pop_datagram(); not datagram.empty();
         datagram = function_->pop_datagram())
    {
        auto* packet = ::net_pkt_rx_alloc_with_buffer(netif_, datagram.size(), NET_AF_UNSPEC,
                                                      NET_IPPROTO_IP, K_MSEC(1));
        if (packet == nullptr)
        {
            // TODO: this will drop the popped datagram, need better handling
            LOG_ERR("Failed to allocate packet for received datagram");
            break;
        }
        if (::net_pkt_write(packet, datagram.data(), datagram.size()) != 0)
        {
            ::net_pkt_unref(packet);
            continue;
        }
        else if (::net_recv_data(netif_, packet) < 0)
        {
            LOG_ERR("Failed to receive data on interface");
            ::net_pkt_unref(packet);
            continue;
        }
    }
}

const usb::cdc::mac_address& cdc_ncm_ethernet::get_address(cdc::ncm::function& function) const
{
    // layout-compatible: usb::cdc::mac_address is std::array<uint8_t, 6>
    return reinterpret_cast<const usb::cdc::mac_address&>(config().remote_mac_address);
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
