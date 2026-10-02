// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "usb/df/config.hpp"
#include "usb/df/message.hpp"

namespace usb::df
{
class device;
}
namespace usb::df::vendor
{
/// @brief  The extension is the interface for vendor specific device extensions.
class extension : public c2usb::interface
{
  public:
    static extension& instance()
    {
        static extension ext;
        return ext;
    }

    virtual void bus_reset([[maybe_unused]] device& dev) {}
    virtual void assign_istrings([[maybe_unused]] device& dev, [[maybe_unused]] istring* index) {}
    virtual bool send_owned_string([[maybe_unused]] const device& dev,
                                   [[maybe_unused]] istring index,
                                   [[maybe_unused]] string_message& smsg) const
    {
        return false;
    }
    [[nodiscard]] virtual config::view_list
    configs_by_speed([[maybe_unused]] const device& dev, [[maybe_unused]] usb::speed speed) const
    {
        return {};
    }
    virtual bool control_setup_request([[maybe_unused]] device& dev, [[maybe_unused]] message& msg)
    {
        return false;
    }
    virtual bool control_data([[maybe_unused]] device& dev, [[maybe_unused]] message& msg)
    {
        return false;
    }
    virtual unsigned bos_capabilities([[maybe_unused]] const device& dev,
                                      [[maybe_unused]] df::buffer& buffer) const
    {
        return 0;
    }

  protected:
    constexpr extension() = default;
};

/// @brief  The extension_set allows the device to manage multiple extensions simultaneously.
class extension_set : public extension
{
    reference_array_view<extension> set_;

  public:
    constexpr explicit extension_set(reference_array_view<extension> extensions)
        : set_(extensions)
    {}
    void bus_reset(device& dev) override
    {
        for (auto* ext : set_)
        {
            ext->bus_reset(dev);
        }
    }
    void assign_istrings(device& dev, istring* index) override
    {
        for (auto* ext : set_)
        {
            ext->assign_istrings(dev, index);
        }
    }
    bool send_owned_string(const device& dev, istring index, string_message& smsg) const override
    {
        for (auto* ext : set_)
        {
            if (ext->send_owned_string(dev, index, smsg))
            {
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] config::view_list configs_by_speed(const device& dev,
                                                     usb::speed speed) const override
    {
        for (auto* ext : set_)
        {
            if (auto configs = ext->configs_by_speed(dev, speed); !configs.empty())
            {
                return configs;
            }
        }
        return {};
    }
    bool control_setup_request(device& dev, message& msg) override
    {
        for (auto* ext : set_)
        {
            if (ext->control_setup_request(dev, msg))
            {
                return true;
            }
        }
        return false;
    }
    bool control_data(device& dev, message& msg) override
    {
        for (auto* ext : set_)
        {
            if (ext->control_data(dev, msg))
            {
                return true;
            }
        }
        return false;
    }
    unsigned bos_capabilities(const device& dev, df::buffer& buffer) const override
    {
        unsigned caps = 0;
        for (auto* ext : set_)
        {
            caps += ext->bos_capabilities(dev, buffer);
        }
        return caps;
    }
};

} // namespace usb::df::vendor
