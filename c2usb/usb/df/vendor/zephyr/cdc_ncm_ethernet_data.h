// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>

struct c2usb_cdc_ncm_ethernet_config
{
    const char* function_name;
};

#ifdef __cplusplus
namespace usb::df::cdc::ncm
{
class function;
}
using c2usb_cdc_ncm_ethernet_function = usb::df::cdc::ncm::function;
#else
typedef struct c2usb_cdc_ncm_ethernet_function c2usb_cdc_ncm_ethernet_function;
#endif

struct c2usb_cdc_ncm_ethernet
{
    const struct c2usb_cdc_ncm_ethernet_config* config_;
    c2usb_cdc_ncm_ethernet_function* function_;
    struct net_if* netif_;
    struct k_work_delayable works_;
    struct k_event events_;
    struct
    {
        const uint8_t* ptr;
        size_t len;
    } rx_dg_;
    uint8_t mac_address[6];
};

/* use identical memory layout in C as the final c2usb_cdc_ncm_ethernet class */
struct c2usb_cdc_ncm_ethernet_driver_data
{
    void* const vtable;
    struct c2usb_cdc_ncm_ethernet driver_data;
};
