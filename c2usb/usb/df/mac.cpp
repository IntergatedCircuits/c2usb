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
#include "usb/df/mac.hpp"
#include "usb/df/device.hpp"
#include "usb/df/function.hpp"

namespace usb::df
{
void mac::init(device& dev_if, const usb::speeds& speeds)
{
    dev_if_ = &dev_if;
    init(speeds);
}

void mac::deinit([[maybe_unused]] device& dev_if)
{
    assert(&dev_if == dev_if_);
    stop();
    deinit();
    dev_if_ = nullptr;
}

void mac::start()
{
    if (!active())
    {
        set_attached(true);
        active_ = true;
    }
}

void mac::stop()
{
    if (active())
    {
        set_attached(false);
        active_ = false;
    }
}

void mac::bus_reset()
{
    std_status_.remote_wakeup = false;
    bool power_change = power_state() != power::state::L0_ON;
    power_state_ = power::state::L0_ON;
    assert(dev_if_ != nullptr);
    dev_if_->on_bus_reset(power_change ? device::event::POWER_STATE_CHANGE : device::event::NONE);
    allocate_endpoints();
}

void mac::set_power_state(power::state new_state)
{
    if (new_state == power_state())
    {
        return;
    }
    power_state_ = new_state;
    assert(dev_if_ != nullptr);
    dev_if_->on_power_state_change(new_state);
}

void mac::set_remote_wakeup(bool enabled)
{
    std_status_.remote_wakeup = enabled;
}
void mac::set_power_source(usb::power::source src)
{
    std_status_.self_powered = (src == usb::power::source::BUS);
}

uint32_t mac::granted_bus_current_uA() const
{
    switch (power_state())
    {
    case power::state::L3_OFF:
        return 0;
    case power::state::L2_SUSPEND:
        return 2'500;
    default:
        if (configured())
        {
            return active_config().info().max_power_mA() * 1'000;
        }
        else
        {
            return 100'000;
        }
    }
}

usb::result mac::remote_wakeup()
{
    if (!std_status().remote_wakeup)
    {
        return usb::result::operation_not_permitted;
    }
    if (power_state() == power::state::L0_ON)
    {
        return usb::result::already_connected;
    }
    if (power_state() == power::state::L3_OFF)
    {
        return usb::result::not_connected;
    }
    return signal_remote_wakeup();
}

void mac::set_config(config::view config)
{
    allocate_endpoints(config);
    active_config_ = config;
}

transfer mac::control_ep_setup()
{
    ctrl_msg_.set_pending();
    dev_if_->on_control_setup(ctrl_msg_);
    assert(!ctrl_msg_.pending_);
    return ctrl_msg_.data_;
}

bool mac::control_ep_data(direction ep_dir, const transfer& t)
{
    if (ep_dir != request().direction())
    {
        return false;
    }
    if (t.size() > 0)
    {
        ctrl_msg_.set_pending(t);
        dev_if_->on_control_data(ctrl_msg_);
        assert(!ctrl_msg_.pending_);
        return !ctrl_msg_.data_.stalled();
    }
    return true;
}

void mac::ep_transfer_complete(endpoint::address addr, ep_handle eph, const transfer& t)
{
    assert(configured());
    ep_address_to_config(addr).interface().function().transfer_complete(eph, t);
}

message* mac::get_pending_message([[maybe_unused]] const function* caller)
{
    assert((caller == nullptr) or
           (configured() and (request().recipient() == control::request::recipient::INTERFACE) and
            (&(active_config().interfaces()[request().wIndex].function()) == caller)));
    return ctrl_msg_.pending_ ? &ctrl_msg_ : nullptr;
}

const config::endpoint& mac::ep_address_to_config(endpoint::address addr) const
{
    return active_config().endpoints().at(addr);
}

ep_handle index_handle_mac::ep_address_to_handle(endpoint::address addr) const
{
    auto& ep = ep_address_to_config(addr);
    if (ep.valid())
    {
        return ep_config_to_handle(ep);
    }
    return {};
}

ep_handle index_handle_mac::ep_config_to_handle(const config::endpoint& ep) const
{
    return create_ep_handle(active_config().endpoints().indexof(ep));
}

ep_handle address_handle_mac::ep_address_to_handle(endpoint::address addr) const
{
    if (configured())
    {
        return create_ep_handle(addr);
    }
    return {};
}

} // namespace usb::df
