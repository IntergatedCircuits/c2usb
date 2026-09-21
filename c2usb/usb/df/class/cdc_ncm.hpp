// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <atomic>
#include <ranges>
#include "usb/class/cdc_ncm_ntb.hpp"
#include "usb/df/class/cdc.hpp"

namespace usb::df::cdc::ncm
{
class function;

struct ntb_buffer
{
    std::span<uint32_t> in;
    std::span<uint32_t> out;
};

enum struct state : uint8_t
{
    DISABLED = 0,
    ENABLED = 1,
    CONNECTED = 3,
};

class network_interface : public c2usb::interface
{
  public:
    using c2usb::interface::interface;

    virtual void state_change(function& func, state old_state, state new_state) = 0;
    virtual void data_received(function& func) = 0;
    virtual void tx_buffer_available([[maybe_unused]] function& func) {}
};

class function : public cdc::function
{
  public:
    [[nodiscard]] df::config::elements<5> config_entry(const config::endpoint& out_ep,
                                                       const config::endpoint& in_ep,
                                                       const config::endpoint& notify_in_ep)
    {
        return cdc::function::config_entry(out_ep, in_ep, notify_in_ep, 1);
    }

    [[nodiscard]] df::config::elements<5>
    config_entry(usb::speed speed, endpoint::address out_ep_addr, endpoint::address in_ep_addr,
                 endpoint::address notify_in_ep_addr, uint8_t notify_in_ep_interval)
    {
        return cdc::function::config_entry(speed, out_ep_addr, in_ep_addr, notify_in_ep_addr,
                                           notify_in_ep_interval, 1);
    }

    /// @brief Create a new CDC NCM function instance
    /// @param if_address: the MAC address assigned to the host interface
    /// @param netif: the network interface to use for this function
    /// @param ntb_buf: the buffers to use for NTB in and out data
    /// @param name: the name of the function
    constexpr function(const usb::cdc::mac_address& if_address, network_interface& netif,
                       const ntb_buffer& ntb_buf, const char_t* name = {})
        : cdc::function(name, extra_string_count),
          if_address_(if_address),
          netif_(netif),
          ntb_in_{ntb_buf.in},
          ntb_out_{ntb_buf.out}
    {
        assert(ntb_in_.buffer_valid());
        assert(ntb_out_.buffer_valid());
    }

    /// @brief  Connect the link with the specified bitrate
    /// @param  bitrate: the bitrate to use for the connection
    /// @return result of the connection message transmission
    c2usb::result connect(uint32_t bitrate);

    /// @brief  Connect the link with an automatically determined bitrate
    /// @return result of the connection message transmission
    c2usb::result connect();

    /// @brief  Disconnect the link
    /// @return result of the disconnection message transmission
    c2usb::result disconnect();

    /// @brief  Retrieve the next datagram received from the host, if any.
    ///         The span's lifetime will be valid until the next call to pop_datagram() or until the
    ///         function is destroyed.
    /// @return a span of the datagram data, or empty if no datagram is available
    std::span<const uint8_t> pop_datagram();

    /// @brief  Allocate space for a datagram to be filled in-place, and to be committed with
    ///         commit_datagram()
    /// @param  size: the size of the datagram to allocate
    /// @param  dg: a span that will be set to the allocated datagram space
    /// @return result indicating success or failure
    c2usb::result allocate_datagram(size_t size, std::span<uint8_t>& dg);

    /// @brief  Commit a datagram that was allocated with allocate_datagram() for transmission
    /// @param  dg: the datagram span returned by the last allocate_datagram() call
    /// @return result indicating transmission success, or failure
    c2usb::result commit_datagram(const std::span<uint8_t>& dg);

    /// @brief  Send a datagram to the host, allocating space and committing it automatically
    /// @tparam TRange: a range type with value type convertible to uint8_t
    /// @param  range: the range of datagram data to send
    /// @return result indicating transmission success, or failure
    template <typename TRange>
    c2usb::result send_datagram(const TRange& range)
        requires(std::ranges::range<TRange>)
    {
        std::span<uint8_t> dg;
        auto result = allocate_datagram(std::ranges::size(range), dg);
        if (result != c2usb::result::ok)
        {
            return result;
        }
        std::copy(std::ranges::begin(range), std::ranges::end(range), dg.begin());
        return commit_datagram(dg);
    }

    /// @brief  Get the current state of the function
    /// @return the current state
    [[nodiscard]] state get_state() const { return state_.load(std::memory_order_acquire); }

    /// @brief  Clean up the network interface and scrub NTB buffers,
    ///         so the function can be safely destroyed.
    /// @return reference to the network interface to allow freeing it
    network_interface& cleanup()
    {
        ntb_in_.scrub();
        ntb_out_.scrub();
        return netif_;
    }

  private:
    static constexpr unsigned NTB_SIZE = 16;

    void describe_config(const config::interface& iface, uint8_t if_index,
                         df::buffer& buffer) const override;
    void send_string(uint8_t rel_index, string_message& smsg) override;
    void control_setup_request(message& msg, const config::interface& iface) override;
    void control_data_complete(message& msg, const config::interface& iface) override;
    void enable(const config::interface& iface, uint8_t alt_sel) override;
    void disable(const config::interface& iface) override;
    void ep_out_callback(const transfer& xfer);
    void ep_in_callback(const transfer& xfer);
    void notify_ep_callback(const transfer& xfer);
    void ep_callback(const transfer& xfer) override
    {
        if (xfer.endpoint() == ep_out_handle())
        {
            ep_out_callback(xfer);
        }
        else
        {
            ep_in_callback(xfer);
        }
    }
    [[nodiscard]] uint8_t get_alt_setting(const config::interface& iface) const override;
    void get_msos2_subset(const config::interface& iface, uint8_t iface_index,
                          df::buffer& buffer) const override;

    static constexpr unsigned extra_string_count = 1; // MAC address as string descriptor
    using flag_type = uint16_t;
    using page_type = uint8_t;
    c2usb::result try_send(flag_type flags_to_clear = 0);

    static constexpr page_type other(page_type page) { return page ^ 1; }
    static constexpr flag_type flags_by_page(flag_type flag, page_type page)
    {
        return flag << flag_type(page);
    }

    static constexpr flag_type ntb_pending_flag = 0x1;
    static constexpr flag_type ntb_manipulate_flag = 0x4;
    static constexpr flag_type ntb_connected_flag = 0x10;
    static constexpr flag_type ntb_clear_to_send_flag = 0x40;
    static constexpr flag_type ntb_receiving_flag = 0x40;
    static constexpr flag_type ntb_notify_flag = 0x100;

    static constexpr flag_type ntb_all_rx_flags =
        (ntb_receiving_flag << flag_type(0)) | (ntb_receiving_flag << flag_type(1));

    bool pop_datagram_from(page_type page, std::span<const uint8_t>& dg);
    bool ntb_receive(page_type page, bool raised_flag = false);

    struct notify_data
    {
        usb::cdc::notification::speed_change speed_change{0};
        usb::cdc::notification::header connection{usb::cdc::notification::code::NETWORK_CONNECTION};
    };
    C2USB_USB_TRANSFER_ALIGN(notify_data, notify_payload_) {};

    const usb::cdc::mac_address& if_address_;
    network_interface& netif_;
    std::span<const uint8_t> ntb2send_;
    std::atomic<flag_type> in_flags_;
    std::atomic<flag_type> out_flags_;
    std::atomic<state> state_;

    usb::cdc::ncm::ntb_tx_ctx<NTB_SIZE> ntb_in_;
    usb::cdc::ncm::ntb_rx_ctx<NTB_SIZE> ntb_out_;
};

constexpr usb::class_info class_info()
{
    return {usb::cdc::CLASS_CODE, usb::cdc::subclass::NETWORK_CONTROL_MODEL,
            usb::cdc::protocol_code::USB};
}
constexpr usb::class_info data_class_info()
{
    return {usb::cdc::data::CLASS_CODE, usb::cdc::data::SUBCLASS_CODE,
            usb::cdc::data::protocol_code::NCM_NTB};
}

} // namespace usb::df::cdc::ncm
