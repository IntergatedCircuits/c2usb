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
#include "usb/df/device.hpp"
#include "usb/df/function.hpp"
#include "usb/standard/descriptors.hpp"

using namespace usb::df;

void device::open()
{
    mac_.start();
}

void device::close()
{
    set_config({});
    mac_.stop();
}

bool device::is_open() const
{
    return mac_.running();
}

void device::delegate_power_event(event ev)
{
    if (power_event_delegate_)
    {
        power_event_delegate_(*this, ev);
    }
}

void device::handle_new_power_state(usb::power::state new_state)
{
    if (new_state == usb::power::state::L3_OFF)
    {
        set_config({});
    }
    delegate_power_event(event::POWER_STATE_CHANGE);
}

const config::power* device::power_config() const
{
    return configured() ? &mac_.active_config().info() : nullptr;
}

void device::handle_reset_request()
{
    set_config({});
    extension_.bus_reset(*this);
}

void device::handle_control_message(message& msg)
{
    using namespace control;

    if (msg.stage() == stage::SETUP)
    {
        switch (msg.request().recipient())
        {
        case control::request::recipient::DEVICE:
            return device_setup_request(msg);

        case control::request::recipient::INTERFACE:
            return interface_control(msg, &function::handle_control_setup);

        case control::request::recipient::ENDPOINT:
            return endpoint_setup_request(msg);

        default:
            return msg.reject();
        }
    }
    else // stage::DATA
    {
        switch (msg.request().recipient())
        {
        case control::request::recipient::DEVICE:
            if (msg.request().type() == usb::control::request::type::STANDARD)
            {
                return msg.confirm();
            }
            else
            {
                return extension_.control_data_status(*this, msg);
            }

        case control::request::recipient::INTERFACE:
            return interface_control(msg, &function::handle_control_data);

        case control::request::recipient::ENDPOINT:
        default:
            // endpoint recipient don't deal with received data,
            // nor do they need steps after sending data
            return msg.confirm();
        }
    }
}

void device::interface_control(message& msg,
                               void (function::*handler)(message&, const config::interface&))
{
    auto ifaces = mac_.active_config().interfaces();
    auto& iface = ifaces[msg.request().wIndex];
    if (iface.valid())
    {
        return (iface.function().*handler)(msg, iface);
    }

    return msg.reject();
}

void device::endpoint_setup_request(message& msg)
{
    endpoint::address addr{msg.request().wIndex.low_byte()};
    auto& ep = mac_.ep_address_to_config(addr);
    if (ep.valid())
    {
        return ep.interface().function().handle_control_setup(msg, mac_.ep_config_to_handle(ep));
    }

    return msg.reject();
}

void device::set_address(message& msg)
{
    if (not configured() and (msg.request().wIndex == 0) and (msg.request().wLength == 0) and
        (msg.request().wValue.low_byte() < 0x80))
    {
        // this request must be handled after the status stage
        // implemented by the MAC independently
        return msg.confirm();
    }
    else
    {
        return msg.reject();
    }
}

void device::set_config(config::view config)
{
    // effective change in configuration
    if (config != mac_.active_config())
    {
        // reverse order to ensure consistency
        for (auto& iface : mac_.active_config().interfaces().reverse())
        {
            iface.function().deinit(iface);
        }

        mac_.set_config(config);
        delegate_power_event(event::CONFIGURATION_CHANGE);

        for (auto& iface : mac_.active_config().interfaces())
        {
            iface.function().init(iface, &mac_);
        }
    }
}

void device::set_configuration(message& msg)
{
    uint8_t config_index = msg.request().wValue.low_byte();
    config::view config;

    if (config_index == 0)
    {
        config = {};
    }
    else
    {
        config = configs_by_speed(bus_speed())[config_index - 1];
        if (not config.valid())
        {
            // invalid index
            return msg.reject();
        }
    }

    set_config(config);

    return msg.confirm();
}

void device::get_configuration(message& msg)
{
    if (configured())
    {
        uint8_t config_index = 0;
        auto configset = configs_by_speed(bus_speed());
        for (auto config : configset)
        {
            config_index++;
            if (config == mac_.active_config())
            {
                return msg.send_value(config_index);
            }
        }
        assert(false);
    }
    return msg.send_value(0);
}

void device::get_status(message& msg)
{
    return msg.send_value(mac_.std_status());
}

void device::set_feature(message& msg, bool active)
{
    using namespace standard::device;

    if (msg.request().wValue == feature::REMOTE_WAKEUP)
    {
        mac_.set_remote_wakeup(active);
        return msg.confirm();
    }
    else
    {
        return msg.reject();
    }
}

void device::device_setup_request(message& msg)
{
    using namespace standard::device;

    if (msg.request().type() == usb::control::request::type::STANDARD)
    {
        switch (msg.request())
        {
        case GET_DESCRIPTOR:
            return get_descriptor(msg);

        case SET_ADDRESS:
            return set_address(msg);

        case SET_CONFIGURATION:
            return set_configuration(msg);

        case GET_CONFIGURATION:
            return get_configuration(msg);

        case GET_STATUS:
            return get_status(msg);

        case SET_FEATURE:
        case CLEAR_FEATURE:
            return set_feature(msg, msg.request() == SET_FEATURE);

        default:
            return msg.reject();
        }
    }
    else
    {
        return extension_.control_setup_request(*this, msg);
    }
}

void device::get_string_descriptor(message& msg)
{
    istring index = msg.request().wValue.low_byte();
    if (index == 0)
    {
        return msg.send_data(language_id_provider<char_t>::descriptor());
    }

    auto& smsg = msg.to_string_message();
    if (extension_.send_owned_string(*this, index, smsg))
    {
        // nothing more to do
    }
    else if (smsg.language_id() == 0)
    {
        // Windows MSOS 1.0 ...
        smsg.reject();
    }
    else if (index < istr_config_base())
    {
        get_function_string(index, smsg);
    }
    else if (index < ISTR_GLOBAL_BASE)
    {
        get_config_string(index, smsg);
    }
    else if (index == ISTR_VENDOR_NAME)
    {
        smsg.send_string(product_info_.vendor_name);
    }
    else if (index == ISTR_PRODUCT_NAME)
    {
        smsg.send_string(product_info_.product_name);
    }
    else if (index == ISTR_SERIAL_NUMBER)
    {
        smsg.send_as_hex_string(product_info_.serial_number);
    }
    else
    {
        smsg.reject();
    }
}

void device::assign_function_istrings()
{
    const auto ss = speeds();
    for (speed s : ss)
    {
        configs_by_speed(s).for_all(&function::free_string_index);
    }

    istring index = 1;
    extension_.assign_istrings(*this, &index);

    for (speed s : ss)
    {
        configs_by_speed(s).for_all(&function::allocate_string_index, &index);
    }
    assert(index < istr_config_base());
}

void device::get_function_string(istring index, string_message& smsg)
{
    for (speed s : speeds())
    {
        if (configs_by_speed(s).until_any(&function::send_owned_string, index, smsg))
        {
            return;
        }
    }
    return smsg.reject();
}

void device::get_config_string(istring index, string_message& smsg)
{
    const auto ss = speeds();
    index -= istr_config_base();
    speed s = ss.at(index / max_config_count());
    auto config = configs_by_speed(s)[index % max_config_count()];
    if (config.info().name() != nullptr)
    {
        return smsg.send_string(config.info().name());
    }
    return smsg.reject();
}

usb::istring device::get_config_istring(uint8_t config_index, usb::speed speed)
{
    const auto ss = speeds();
    istring istr_config = config_index;
    istr_config += ss.offset(speed) * max_config_count();
    istr_config += istr_config_base();
    return istr_config;
}

void device::get_config_descriptor(message& msg, usb::speed speed)
{
    uint8_t config_index = msg.request().wValue.low_byte();

    auto config = configs_by_speed(speed)[config_index];
    if (not config.valid())
    {
        return msg.reject();
    }

    auto* config_desc = msg.buffer().allocate<standard::descriptor::configuration>();

    // either CONFIGURATION or OTHER_SPEED_CONFIGURATION
    config_desc->bDescriptorType = msg.request().wValue.high_byte();
    // offset by 1 so SET_CONFIGURATION 0 can clear any configuration
    config_desc->bConfigurationValue = 1 + config_index;

    config_desc << config.info();
    if (config.info().name() != nullptr)
    {
        config_desc->iConfiguration = get_config_istring(config_index, speed);
    }

    // the functions fill out their own parts of the descriptor
    for (const config::interface& iface : config.interfaces())
    {
        iface.function().describe_config(iface, config_desc->bNumInterfaces, msg.buffer());
        config_desc->bNumInterfaces++;
    }

    config_desc->wTotalLength = msg.buffer().used_length();

    return msg.send_buffer();
}

void device::get_device_descriptor(message& msg)
{
    auto* dev_desc = msg.buffer().allocate<standard::descriptor::device>();

    dev_desc->bcdUSB = usb_spec_version();
    dev_desc->bMaxPacketSize = mac_.control_ep_max_packet_size(bus_speed());
    dev_desc->bNumConfigurations = configs_by_speed(bus_speed()).size();

    dev_desc->idVendor = product_info_.vendor_id;
    dev_desc->idProduct = product_info_.product_id;
    dev_desc->bcdDevice = product_info_.product_version;

    // serial number is optional, the others are mandatory
    if (not product_info_.serial_number.empty())
    {
        dev_desc->iSerialNumber = ISTR_SERIAL_NUMBER;
    }
    assert(product_info_.vendor_name != nullptr);
    dev_desc->iManufacturer = ISTR_VENDOR_NAME;
    assert(product_info_.product_name != nullptr);
    dev_desc->iProduct = ISTR_PRODUCT_NAME;

    return msg.send_buffer();
}

void device::get_device_qualifier_descriptor(message& msg, usb::speed speed)
{
    auto* dq_desc = msg.buffer().allocate<standard::descriptor::device_qualifier>();

    dq_desc->bcdUSB = usb_spec_version();
    dq_desc->bMaxPacketSize = mac_.control_ep_max_packet_size(speed);
    dq_desc->bNumConfigurations = configs_by_speed(speed).size();

    return msg.send_buffer();
}

void device::get_bos_descriptor(message& msg)
{
    using namespace standard::descriptor;

    auto* bos_desc = msg.buffer().allocate<binary_object_store>();
    msg.buffer().allocate<device_capability::usb_2p0_extension>(mac_.lpm_support());

    bos_desc->bNumDeviceCaps = 1 + extension_.bos_capabilities(*this, msg.buffer());
    bos_desc->wTotalLength = msg.buffer().used_length();

    return msg.send_buffer();
}

void device::get_descriptor(message& msg)
{
    using namespace standard::descriptor;

    switch (static_cast<type>(msg.request().wValue.high_byte()))
    {
    case type::DEVICE:
        return get_device_descriptor(msg);

    case type::CONFIGURATION:
        return get_config_descriptor(msg, bus_speed());

    case type::STRING:
        return get_string_descriptor(msg);

    case type::BINARY_OBJECT_STORE:
        return get_bos_descriptor(msg);

    default:
        break;
    }

    return msg.reject();
}

void device::get_descriptor_dual_speed(message& msg)
{
    using namespace standard::descriptor;
    auto alternative_speed = bus_speed() == speed::HIGH ? speed::FULL : speed::HIGH;

    switch (static_cast<type>(msg.request().wValue.high_byte()))
    {
    case type::DEVICE_QUALIFIER:
        return get_device_qualifier_descriptor(msg, alternative_speed);

    case type::OTHER_SPEED_CONFIGURATION:
        return get_config_descriptor(msg, alternative_speed);

    default:
        return device::get_descriptor(msg);
    }
}

template <>
void device::get_descriptor_by_speed_support<false>(message& msg)
{
    return device::get_descriptor(msg);
}

template <>
void device::get_descriptor_by_speed_support<true>(message& msg)
{
    return get_descriptor_dual_speed(msg);
}
