// SPDX-License-Identifier: MPL-2.0
#define DT_DRV_COMPAT c2usb_cdc_ncm_ethernet

#include "usb/df/vendor/zephyr/cdc_ncm_ethernet_data.h"
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/net/ethernet.h>

extern int c2usb_cdc_ncm_ethernet_init(const struct device* device);
extern const struct ethernet_api c2usb_cdc_ncm_ethernet_api;

#define C2USB_CDC_NCM_ETHERNET_DEFINE(instance)                                                    \
    static const struct c2usb_cdc_ncm_ethernet_config c2usb_cdc_ncm_ethernet_config_##instance = { \
        .local_mac_address = DT_INST_PROP(instance, local_mac_address),                            \
        .remote_mac_address = DT_INST_PROP(instance, remote_mac_address),                          \
        .function_name = DT_INST_PROP_OR(instance, function_name, NULL),                           \
    };                                                                                             \
    static struct c2usb_cdc_ncm_ethernet_driver_data c2usb_cdc_ncm_ethernet_data_##instance;       \
    ETH_NET_DEVICE_DT_INST_DEFINE(                                                                 \
        instance, c2usb_cdc_ncm_ethernet_init, NULL, &c2usb_cdc_ncm_ethernet_data_##instance,      \
        &c2usb_cdc_ncm_ethernet_config_##instance, CONFIG_ETH_INIT_PRIORITY,                       \
        &c2usb_cdc_ncm_ethernet_api, NET_ETH_MTU)

DT_INST_FOREACH_STATUS_OKAY(C2USB_CDC_NCM_ETHERNET_DEFINE)
