// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "usb/class/cdc.hpp"
#include "usb/df/function.hpp"

namespace usb::standard::descriptor
{
struct interface;
}

namespace usb::df::cdc
{
class function : public df::named_function
{
  protected:
    using df::named_function::named_function;

    [[nodiscard]] standard::descriptor::interface*
    get_base_functional_descriptors(class_info cinfo, uint8_t if_index, df::buffer& buffer) const;

    void open_notify_ep(const config::interface& iface)
    {
        assert(iface.primary());
        if (const auto& ep = iface.endpoints()[0]; ep.valid())
        {
            notify_eph_ = open_ep(ep);
        }
    }

    void open_data_eps(const config::interface& iface)
    {
        assert(!iface.primary());
        assert(iface.endpoints().size() == 2);
        in_ep_mps_ = iface.endpoints()[0].wMaxPacketSize;
        open_eps(iface.endpoints(), data_ephs_);
    }

    result notify(const std::span<const uint8_t>& data)
    {
        return send_ep(ep_notify_handle(), data);
    }
    result notify(const usb::cdc::notification::header& data)
    {
        return notify( // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data),
                                     sizeof(usb::cdc::notification::header) + data.wLength));
    }
    result send_data(const std::span<const uint8_t>& data) { return send_ep(ep_in_handle(), data); }
    result receive_data(const std::span<uint8_t>& data)
    {
        return receive_ep(ep_out_handle(), data);
    }

    [[nodiscard]] ep_handle ep_out_handle() const { return data_ephs_[0]; }
    [[nodiscard]] ep_handle ep_in_handle() const { return data_ephs_[1]; }
    [[nodiscard]] ep_handle ep_notify_handle() const { return notify_eph_; }

    void disable(const config::interface& iface) override
    {
        if (iface.primary())
        {
            close_ep(notify_eph_);
        }
        else
        {
            close_eps(data_ephs_);
        }
    }

    [[nodiscard]] auto in_ep_mps() const { return in_ep_mps_; }
    result send_trailing_zlp_if_needed(const transfer& xfer)
    {
        if (not xfer.empty() and ((xfer.size() % in_ep_mps_) == 0))
        {
            return send_data({});
        }
        return std::errc::message_size;
    }

  private:
    uint16_t in_ep_mps_{};
    std::array<ep_handle, 2> data_ephs_{};
    ep_handle notify_eph_;
};

} // namespace usb::df::cdc
