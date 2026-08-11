// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <chrono>
#include "usb/class/dfu.hpp"
#include "usb/df/function.hpp"

namespace usb::df::dfu
{
class runtime_function : public df::named_function
{
  public:
    [[nodiscard]] df::config::elements<1> config_entry()
    {
        return config::to_elements({df::config::interface{*this}});
    }

    /// @brief  Create a new DFU runtime function instance
    /// @param  name: the name of the DFU runtime function instance.
    /// @param  detach_cbk: the callback function to call when a DFU_DETACH request is received.
    /// @param  detach_timeout: when a non-zero timeout is specified, the device will use
    ///         willDetach = false, and will wait for the host to issue a USB reset after DFU_DETACH
    ///         request within this timeframe.
    template <typename T = uint16_t>
    constexpr runtime_function(
        const char_t* name, void (*detach_cbk)(std::chrono::milliseconds),
        std::chrono::duration<T, std::milli> detach_timeout = std::chrono::milliseconds(0))
        : df::named_function(name), detach_cbk_(detach_cbk), detach_timeout_(detach_timeout)
    {}

    /// @brief  Create a new DFU runtime function instance
    /// @param  name: the name of the DFU runtime function instance.
    /// @param  detach_cbk: the callback function to call when a DFU_DETACH request is received.
    constexpr runtime_function(const char_t* name, void (*detach_cbk)(std::chrono::milliseconds))
        : df::named_function(name), detach_cbk_(detach_cbk)
    {}

  private:
    void (*detach_cbk_)(std::chrono::milliseconds);
    std::chrono::duration<uint16_t, std::milli> detach_timeout_{};
    usb::dfu::state state_{usb::dfu::state::APP_IDLE};

    void describe_config(const config::interface& iface, uint8_t if_index,
                         df::buffer& buffer) override;

    void enable([[maybe_unused]] const config::interface& iface,
                [[maybe_unused]] uint8_t alt_sel) override
    {
        state_ = usb::dfu::state::APP_IDLE;
    }
    void control_setup_request(message& msg, const config::interface& iface) override;

    [[nodiscard]] std::string_view ms_compatible_id() const override { return {"WINUSB"}; }
};

} // namespace usb::df::dfu
