/// @file
///
/// @author Benedek Kupper
/// @date   2023
///
/// @copyright
///         This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
///         If a copy of the MPL was not distributed with this file, You can obtain one at
///         https://mozilla.org/MPL/2.0/.
///
#ifndef __USB_DF_CLASS_CDC_ACM_HPP_
#define __USB_DF_CLASS_CDC_ACM_HPP_

#include "usb/df/class/cdc.hpp"

namespace usb::df::cdc::acm
{
constexpr inline usb::class_info class_info()
{
    return {usb::cdc::CLASS_CODE, usb::cdc::subclass::ABSTRACT_CONTROL_MODEL,
            usb::cdc::protocol_code::ITU_T_Vp250};
}
constexpr inline usb::class_info data_class_info()
{
    return {usb::cdc::data::CLASS_CODE, usb::cdc::data::SUBCLASS_CODE,
            usb::cdc::data::protocol_code::USB};
}

class function : public cdc::function
{
  public:
    using capabilities = usb::cdc::descriptor::abstract_control_management::capabilities;

    constexpr function(const char_t* name = {})
        : cdc::function(name)
    {}

  private:
    void describe_config(const config::interface& iface, uint8_t if_index,
                         df::buffer& buffer) override;

    void control_setup_request(message& msg, const config::interface& iface) override;
    void control_data_complete(message& msg, const config::interface& iface) override;

    void start(const config::interface& iface, uint8_t alt_sel) override;

    void transfer_complete(ep_handle eph, const transfer& t) override;

    C2USB_USB_TRANSFER_ALIGN(usb::cdc::serial::line_coding, line_coding_){};
    uint16_t in_ep_mps_{};
    uint16_t tx_len_{};
};
} // namespace usb::df::cdc::acm

#endif // __USB_DF_CLASS_CDC_ACM_HPP_
