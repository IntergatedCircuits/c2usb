// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>

struct c2usb_cdc_ncm_ethernet_config
{
    uint8_t local_mac_address[6];
    uint8_t remote_mac_address[6];
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
    struct k_work_delayable link_work_;
    struct k_event events_;
    uint8_t if_mac_address[6];
};

/* use identical memory layout in C as the final c2usb_cdc_ncm_ethernet class */
struct c2usb_cdc_ncm_ethernet_driver_data
{
    void* const vtable;
    struct c2usb_cdc_ncm_ethernet driver_data;
};
