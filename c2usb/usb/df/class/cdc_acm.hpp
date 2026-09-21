// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "usb/df/class/cdc.hpp"

namespace usb::df::cdc::acm
{
constexpr usb::class_info class_info()
{
    return {usb::cdc::CLASS_CODE, usb::cdc::subclass::ABSTRACT_CONTROL_MODEL,
            usb::cdc::protocol_code::ITU_T_Vp250};
}
constexpr usb::class_info data_class_info()
{
    return {usb::cdc::data::CLASS_CODE, usb::cdc::data::SUBCLASS_CODE,
            usb::cdc::data::protocol_code::USB};
}

struct line_config : public usb::cdc::serial::line_coding
{
    uint8_t bControlLineState{};

    [[nodiscard]] bool data_terminal_ready() const { return (bControlLineState & 1) != 0; }
    [[nodiscard]] bool request_to_send() const { return (bControlLineState & 2) != 0; }
};

enum class line_event : uint8_t
{
    STATE_CHANGE = 0,
    CODING_CHANGE = 1,
};

class function : public cdc::function
{
  public:
    // no notification endpoint
    [[nodiscard]] df::config::elements<4> config_entry(const config::endpoint& out_ep,
                                                       const config::endpoint& in_ep)
    {
        assert((out_ep.address().direction() == direction::OUT) and
               (in_ep.address().direction() == direction::IN));
        return config::to_elements(
            {config::interface(*this, 0), config::interface(*this, 1), out_ep, in_ep});
    }

    // active notification endpoint
    [[nodiscard]] df::config::elements<5> config_entry(const config::endpoint& out_ep,
                                                       const config::endpoint& in_ep,
                                                       const config::endpoint& notify_in_ep)
    {
        assert((out_ep.address().direction() == direction::OUT) and
               (in_ep.address().direction() == direction::IN) and
               (notify_in_ep.address().direction() == direction::IN));
        return config::to_elements({config::interface(*this, 0), notify_in_ep,
                                    config::interface(*this, 1), out_ep, in_ep});
    }

    // active notification endpoint
    [[nodiscard]] df::config::elements<5>
    config_entry(usb::speed speed, endpoint::address out_ep_addr, endpoint::address in_ep_addr,
                 endpoint::address notify_in_ep_addr, uint8_t notify_in_ep_interval)
    {
        return config_entry(config::endpoint::bulk(out_ep_addr, speed, true),
                            config::endpoint::bulk(in_ep_addr, speed),
                            config::endpoint::interrupt(notify_in_ep_addr,
                                                        sizeof(usb::cdc::notification::header),
                                                        notify_in_ep_interval));
    }

    // unused notification endpoint
    [[nodiscard]] df::config::elements<5> config_entry(usb::speed speed,
                                                       endpoint::address out_ep_addr,
                                                       endpoint::address in_ep_addr,
                                                       endpoint::address notify_in_ep_addr)
    {
        return config_entry(
            config::endpoint::bulk(out_ep_addr, speed, true),
            config::endpoint::bulk(in_ep_addr, speed),
            config::endpoint(
                config::endpoint::interrupt(
                    notify_in_ep_addr, sizeof(usb::cdc::notification::header),
                    endpoint::interval::from_rate(speed, std::chrono::milliseconds(128))),
                true));
    }

    using line_config = acm::line_config;
    using line_event = acm::line_event;

    constexpr function(const char_t* name = {})
        : cdc::function(name)
    {}

    virtual void set_line([[maybe_unused]] const line_config& cfg, [[maybe_unused]] line_event ev)
    {}
    virtual void reset_line() {}
    [[nodiscard]] auto& get_line_config() const { return (line_config_); }

    using cdc::function::notify;
    using cdc::function::send_data;
    virtual void data_sent([[maybe_unused]] const std::span<const uint8_t>& tx,
                           [[maybe_unused]] bool needs_zlp)
    {
        // if more data was produced, send that
        // else if needs_zlp: send_data({});
    }
    using cdc::function::receive_data;
    virtual void data_received([[maybe_unused]] const std::span<uint8_t>& rx) {}

  private:
    using capabilities = usb::cdc::descriptor::abstract_control_management::capabilities;

    void describe_config(const config::interface& iface, uint8_t if_index,
                         df::buffer& buffer) const override;
    void control_setup_request(message& msg, const config::interface& iface) override;
    void control_data_complete(message& msg, const config::interface& iface) override;
    void enable(const config::interface& iface, uint8_t alt_sel) override;
    void disable(const config::interface& iface) override;
    void ep_callback(const transfer& xfer) override;

    [[nodiscard]] auto& line_coding() const
    {
        return static_cast<const usb::cdc::serial::line_coding&>(line_config_);
    }
    [[nodiscard]] auto& line_coding()
    {
        return static_cast<usb::cdc::serial::line_coding&>(line_config_);
    }

    C2USB_USB_TRANSFER_ALIGN(line_config, line_config_) {};
};

} // namespace usb::df::cdc::acm
