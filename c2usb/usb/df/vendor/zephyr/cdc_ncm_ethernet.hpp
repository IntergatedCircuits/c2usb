// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "usb/df/class/cdc_ncm.hpp"
#include "usb/df/vendor/zephyr/cdc_ncm_ethernet_data.h"
#include <zephyr/device.h>
#include <zephyr/event_group.hpp>
#include <zephyr/kernel.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/work_queue.hpp>

extern "C" int c2usb_cdc_ncm_ethernet_init(const ::device* dev);

// for a full-featured compile-time string implementation, see
// https://github.com/intel/cpp-std-extensions/blob/main/include/stdx/ct_string.hpp
template <std::size_t N>
struct ct_string
{
    consteval ct_string() = default;

    consteval ct_string(const char (&str)[N])
    {
        std::copy(std::begin(str), std::end(str), value.begin());
    }

    std::array<char, N> value{};
};

namespace usb::df::zephyr
{
class cdc_ncm_ethernet final : private cdc::ncm::network_interface, private ::c2usb_cdc_ncm_ethernet
{
    friend int ::c2usb_cdc_ncm_ethernet_init(const ::device* dev);

    cdc_ncm_ethernet(const ::device* dev);
    static cdc_ncm_ethernet* from_device(const ::device* dev);
    static cdc_ncm_ethernet& by_device(const ::device* dev)
    {
        return *static_cast<cdc_ncm_ethernet*>(dev->data);
    }
    const c2usb_cdc_ncm_ethernet_config& config() const { return *config_; }

  public:
    /// @brief  Constructs the NCM function bound to the device at node_id.
    /// @tparam FullName: The full name of the device tree node, acquired with
    ///         DT_NODE_FULL_NAME(node_id)
    /// @param  if_address: the MAC address assigned to the host interface
    /// @param  ntb_buffer: the NTB buffer pair for the NCM function
    /// @param  dev: the Zephyr device instance, acquired with DEVICE_DT_GET(node_id)
    /// @return Pointer to the constructed NCM function, or nullptr on failure
    template <ct_string FullName>
    static cdc::ncm::function*
    construct(const usb::cdc::mac_address& if_address, const cdc::ncm::ntb_buffer& ntb_buffer,
              const ::device* dev = device_get_binding(FullName.value.data()))
    {
        auto* self = from_device(dev);
        if (self == nullptr)
        {
            return nullptr;
        }
        if (self->function_ != nullptr)
        {
            // TODO: assert instead, and return the existing function
            return nullptr;
        }
        // static function instance for each template instantiation
        static cdc::ncm::function function{if_address, *self, ntb_buffer,
                                           self->config().function_name};
        self->function_ = &function;
        return &function;
    }

    /// @brief  Destructs the given NCM function created with construct(), releasing its resources
    ///         and de-associating it from the network interface.
    /// @param  function: The NCM function to destruct
    static void destruct(cdc::ncm::function& function);

    // Zephyr device/ethernet_api callbacks; addresses of these are used directly
    // as the C ABI function pointers of c2usb_cdc_ncm_ethernet_api and init_fn.
    static void driver_iface_init(::net_if* netif);
    static int driver_start(const ::device* dev);
    static int driver_stop(const ::device* dev);
    static ::ethernet_hw_caps driver_capabilities(const ::device*);
    static int driver_set_config(const ::device* dev, ::ethernet_config_type type,
                                 const ::ethernet_config* config);
    static int driver_send(const ::device* dev, ::net_pkt* packet);

  private:
    static constexpr uint32_t NEW_STATE_FLAG = 1;
    static constexpr uint32_t TX_DONE_FLAG = 2;

    ::zephyr::work_delayable& work_handle()
    {
        return static_cast<::zephyr::work_delayable&>(works_);
    }
    ::zephyr::event_group& events_handle() { return static_cast<::zephyr::event_group&>(events_); }

    static void perform_work_handler(::k_work* work)
    {
        c2usb_cdc_ncm_ethernet_driver_data* driver_data =
            CONTAINER_OF(work, c2usb_cdc_ncm_ethernet_driver_data, driver_data.works_);
        auto& self = *reinterpret_cast<cdc_ncm_ethernet*>(driver_data);
        self.perform_work();
    }
    void perform_work();

    std::span<const uint8_t>& rx_datagram()
    {
        static_assert(sizeof(rx_dg_) == sizeof(std::span<const uint8_t>) and
                      alignof(rx_dg_) == alignof(std::span<const uint8_t>));
        return *reinterpret_cast<std::span<const uint8_t>*>(&rx_dg_);
    }

    void state_change(cdc::ncm::function& function, cdc::ncm::state old_state,
                      cdc::ncm::state new_state) override;
    void data_received(cdc::ncm::function& function) override;
    void tx_buffer_available(cdc::ncm::function& function) override;
};

// cdc_ncm_ethernet must be exactly its vtable pointer plus c2usb_cdc_ncm_ethernet's
// data, so that device->data can be reinterpreted as the C++ object in place.
static_assert(sizeof(cdc_ncm_ethernet) == sizeof(::c2usb_cdc_ncm_ethernet_driver_data) and
              alignof(c2usb_cdc_ncm_ethernet_driver_data) == alignof(cdc_ncm_ethernet));

} // namespace usb::df::zephyr
