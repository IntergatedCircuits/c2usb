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

struct line_config : public usb::cdc::serial::line_coding
{
    uint8_t bControlLineState;

    bool data_terminal_ready() const { return bControlLineState & 1; }
    bool request_to_send() const { return bControlLineState & 2; }
};

enum class line_event : uint8_t
{
    STATE_CHANGE = 0,
    CODING_CHANGE = 1,
};

class function : public cdc::function
{
  public:
    constexpr function(const char_t* name = {})
        : cdc::function(name)
    {}

    virtual void set_line(const line_config& cfg, line_event ev) {}
    virtual void reset_line() {}
    auto& get_line_config() const { return (line_config_); }

    using cdc::function::notify;
    using cdc::function::send_data;
    virtual void data_sent(const std::span<const uint8_t>& tx, bool needs_zlp)
    {
        // if more data was produced, send that
        // else if needs_zlp: send_data({});
    }
    using cdc::function::receive_data;
    virtual void data_received(const std::span<uint8_t>& rx) {}
    auto in_ep_mps() const { return in_ep_mps_; }

  private:
    using capabilities = usb::cdc::descriptor::abstract_control_management::capabilities;

    void describe_config(const config::interface& iface, uint8_t if_index,
                         df::buffer& buffer) override;
    void control_setup_request(message& msg, const config::interface& iface) override;
    void control_data_complete(message& msg, const config::interface& iface) override;
    void start(const config::interface& iface, uint8_t alt_sel) override;
    void transfer_complete(ep_handle eph, const transfer& t) override;

    auto& line_coding() const
    {
        return static_cast<const usb::cdc::serial::line_coding&>(line_config_);
    }
    auto& line_coding() { return static_cast<usb::cdc::serial::line_coding&>(line_config_); }

    C2USB_USB_TRANSFER_ALIGN(line_config, line_config_){};
    uint16_t in_ep_mps_{};
    uint16_t tx_len_{};
};

} // namespace usb::df::cdc::acm

#endif // __USB_DF_CLASS_CDC_ACM_HPP_
