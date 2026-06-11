// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <atomic>
#include <ranges>
#include "usb/class/cdc_ncm_ntb.hpp"
#include "usb/df/class/cdc.hpp"

namespace usb::df::cdc::ncm
{
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

class function;

struct ntb_buffer
{
    std::span<uint32_t> in;
    std::span<uint32_t> out;
};

class network_interface : public c2usb::interface
{
  public:
    using c2usb::interface::interface;

    virtual const usb::cdc::mac_address& get_address(function& func) const = 0;

    virtual void enable(function& func) = 0;
    virtual void disable([[maybe_unused]] function& func) {}

    virtual void data_received([[maybe_unused]] function& func) = 0;
    virtual void tx_buffer_available([[maybe_unused]] function& func) {}
};

class function : public cdc::function
{
  public:
    static constexpr unsigned NTB_SIZE = 16;

    /// @brief Create a new CDC NCM function instance
    /// @param netif: the network interface to use for this function
    /// @param ntb_buf: the buffers to use for NTB in and out data
    /// @param name: the name of the function
    constexpr function(network_interface& netif, const ntb_buffer& ntb_buf, const char_t* name = {})
        : cdc::function(name, extra_string_count),
          netif_(netif),
          ntb_in_{ntb_buf.in},
          ntb_out_{ntb_buf.out}
    {
        assert(ntb_in_.buffer_valid());
        assert(ntb_out_.buffer_valid());
    }

    c2usb::result connect(uint32_t bitrate);
    c2usb::result disconnect();

    /// @brief  Retrieve the next datagram received from the host, if any
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

  private:
    void describe_config(const config::interface& iface, uint8_t if_index,
                         df::buffer& buffer) const override;
    void send_string(uint8_t rel_index, string_message& smsg) override;
    void control_setup_request(message& msg, const config::interface& iface) override;
    void control_data_complete(message& msg, const config::interface& iface) override;
    void enable(const config::interface& iface, uint8_t alt_sel) override;
    void disable(const config::interface& iface) override;
    void ep_out_callback(const transfer& t);
    void ep_in_callback(const transfer& t);
    void ep_callback(const transfer& t) override
    {
        if (t.endpoint() == ep_out_handle())
        {
            ep_out_callback(t);
        }
        else
        {
            ep_in_callback(t);
        }
    }
    [[nodiscard]] uint8_t get_alt_setting(const config::interface& iface) const override;
    [[nodiscard]] std::string_view ms_compatible_id() const override { return {"WINNCM"}; }

    static constexpr uint8_t active_alt_setting = 1;
    static constexpr unsigned extra_string_count = 1; // MAC address as string descriptor
    static constexpr istring mac_address_string_index = 1;
    [[nodiscard]] istring mac_address_string() const
    {
        return to_istring(mac_address_string_index);
    }
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
    static constexpr flag_type ntb_ready_to_rx_flag = 0x40;
    static constexpr flag_type ntb_notify_flag = 0x100;

    static constexpr flag_type ntb_no_rx_flags =
        (ntb_ready_to_rx_flag << flag_type(0)) | (ntb_ready_to_rx_flag << flag_type(1));

    struct notify_data
    {
        usb::cdc::notification::speed_change speed_change{0};
        usb::cdc::notification::header connection{usb::cdc::notification::code::NETWORK_CONNECTION};
    };
    C2USB_USB_TRANSFER_ALIGN(notify_data, notify_payload_) {};
    [[nodiscard]] bool connected() const { return notify_payload_.connection.wValue != 0; }

    bool pop_datagram_from(page_type page, std::span<const uint8_t>& dg);
    bool ntb_receive(page_type page);

    network_interface& netif_;
    std::span<const uint8_t> ntb2send_;
    std::atomic<flag_type> in_flags_;
    std::atomic<flag_type> out_flags_;

    usb::cdc::ncm::ntb_tx_ctx<NTB_SIZE> ntb_in_;
    usb::cdc::ncm::ntb_rx_ctx<NTB_SIZE> ntb_out_;
    bool selected_{};
};

} // namespace usb::df::cdc::ncm
