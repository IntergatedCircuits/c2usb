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
#include "usb/df/function.hpp"

using namespace usb::df;

void mac::init(device_interface& dev_if, const usb::speeds& speeds)
{
    dev_if_ = &dev_if;
    init(speeds);
}

void mac::deinit(device_interface& dev_if)
{
    assert(&dev_if == dev_if_);
    set_power_state(power::state::L3_OFF);
    deinit();
    dev_if_ = nullptr;
}

void mac::start()
{
    soft_attach();
    running_ = true;
}

void mac::stop()
{
    running_ = false;
    soft_detach();
}

void mac::bus_reset()
{
    assert(dev_if_ != nullptr);
    dev_if_->handle_reset_request();
    std_status_.remote_wakeup = false;
    allocate_endpoints();
    control_ep_open();
    set_power_state(power::state::L0_ON);
}

void mac::set_power_state(power::state new_state)
{
    if (new_state == power_state())
    {
        return;
    }
    assert(dev_if_ != nullptr);
    power_state_ = new_state;
    dev_if_->handle_new_power_state(new_state);
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

bool mac::remote_wakeup()
{
    if (not std_status().remote_wakeup)
    {
        return false;
    }
    signal_remote_wakeup();
    return true;
}

void mac::set_config(config::view config)
{
    allocate_endpoints(config);
    active_config_ = config;
}

void mac::control_ep_setup()
{
    message::enter_setup();
    dev_if_->handle_control_message(*this);
}

void mac::control_ep_data(direction ep_dir, const transfer& t)
{
    if ((ep_dir == request().direction()) and (t.size() > 0))
    {
        message::enter_data(t);
        dev_if_->handle_control_message(*this);
    }
    else
    {
        message::enter_status();
    }
}

void mac::ep_transfer_complete(ep_handle eph, const transfer& t)
{
    assert(configured());
    ep_handle_to_config(eph).interface().function().transfer_complete(eph, t);
}

message* mac::get_pending_message(function* caller)
{
    assert((caller == nullptr) or
       (configured() and (request().recipient() == control::request::recipient::INTERFACE) and
        (&(active_config().interfaces()[request().wIndex].function()) == caller)));
    return pending_reply() ? this : nullptr;
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

const config::endpoint& index_handle_mac::ep_handle_to_config(ep_handle eph) const
{
    assert(configured());
    return active_config().endpoints()[eph];
}

usb::endpoint::address index_handle_mac::ep_handle_to_address(ep_handle eph) const
{
    return ep_handle_to_config(eph).address();
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

const config::endpoint& address_handle_mac::ep_handle_to_config(ep_handle eph) const
{
    auto& ep = ep_address_to_config(ep_handle_to_address(eph));
    assert(ep.valid());
    return ep;
}
