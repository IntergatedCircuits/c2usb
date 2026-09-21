// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <stdx/ct_string.hpp>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/net/ethernet.h>

#include "usb/df/class/cdc_ncm.hpp"
#include "usb/df/vendor/zephyr/cdc_ncm_ethernet_data.h"

extern "C" int c2usb_cdc_ncm_ethernet_init(const ::device* dev);

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
    /// @param  ntb_buffer: The NTB buffer pair for the NCM function
    /// @param  dev: The Zephyr device instance, acquired with DEVICE_DT_GET(node_id)
    /// @return Pointer to the constructed NCM function, or nullptr on failure
    template <stdx::ct_string FullName>
    static cdc::ncm::function*
    construct(const cdc::ncm::ntb_buffer& ntb_buffer,
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
        static cdc::ncm::function function{*self, ntb_buffer, self->config().function_name};
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

    static void link_state_update_work(::k_work* work)
    {
        c2usb_cdc_ncm_ethernet_driver_data* driver_data =
            CONTAINER_OF(work, c2usb_cdc_ncm_ethernet_driver_data, driver_data.link_work_);
        auto& self = *reinterpret_cast<cdc_ncm_ethernet*>(driver_data);
        self.link_state_update();
    }
    void link_state_update();

    const usb::cdc::mac_address& get_address(cdc::ncm::function& function) const override;
    void state_change(cdc::ncm::function& function, cdc::ncm::state old_state,
                      cdc::ncm::state new_state) override;
    void data_received(cdc::ncm::function& function) override;
    void tx_buffer_available(cdc::ncm::function& function) override;
};

// cdc_ncm_ethernet must be exactly its vtable pointer plus c2usb_cdc_ncm_ethernet's
// data, so that device->data can be reinterpreted as the C++ object in place.
static_assert(sizeof(cdc_ncm_ethernet) == sizeof(::c2usb_cdc_ncm_ethernet_driver_data));

} // namespace usb::df::zephyr
