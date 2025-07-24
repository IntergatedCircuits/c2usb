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
#include "usb/df/vendor/microsoft_os_extension.hpp"
#include "usb/df/function.hpp"

namespace usb::df::microsoft
{
void descriptors::get_msos2_function_subset(const config::interface& iface, uint8_t iface_index,
                                            df::buffer& buffer)
{
    // the only interesting thing is the compatible ID
    auto* compat_id = iface.function().ms_compatible_id();
    if (compat_id == nullptr)
    {
        return;
    }
    auto* func_header = buffer.allocate<usb::microsoft::function_subset_header>();
    func_header->bFirstInterface = iface_index;

    auto* comp_id_desc = buffer.allocate<usb::microsoft::compatible_id>();
    std::string_view(compat_id).copy(comp_id_desc->CompatibleID,
                                     sizeof(comp_id_desc->CompatibleID));

    // When finished with the contents, save the total size of the subset
    func_header->wSubsetLength =
        std::distance(reinterpret_cast<uint8_t*>(func_header), buffer.end());
#if 0
    if (func_header->wSubsetLength <= func_header->size())
    {
        // If no features are added, roll back this subset
        return buffer.free(func_header->wSubsetLength);
    }
#endif
}

void descriptors::get_msos2_config_subset(const config::view& config, uint8_t config_index,
                                          df::buffer& buffer)
{
    auto* conf_header = buffer.allocate<usb::microsoft::config_subset_header>();
    conf_header->bConfigurationValue = config_index;

    uint8_t iface_count = 0;
    for (auto& iface : config.interfaces())
    {
        if (iface.primary())
        {
            get_msos2_function_subset(iface, iface_count, buffer);
        }
        iface_count++;
    }

    // When finished with the contents, save the total size of the subset
    conf_header->wTotalLength =
        std::distance(reinterpret_cast<uint8_t*>(conf_header), buffer.end());
    if (conf_header->wTotalLength <= conf_header->size())
    {
        // If no features are added, roll back this subset
        return buffer.free(conf_header->wTotalLength);
    }
}

void descriptors::get_msos2_descriptor(device& dev, df::buffer& buffer)
{
    auto* set_header = buffer.allocate<usb::microsoft::set_header>();

    uint8_t config_count = 0;
    for (auto config : dev.configs_by_speed(dev.bus_speed()))
    {
        get_msos2_config_subset(config, config_count, buffer);
        config_count++;
    }

    // When finished with the contents, save the total size of the set
    set_header->wTotalLength = std::distance(reinterpret_cast<uint8_t*>(set_header), buffer.end());
    if (set_header->wTotalLength <= set_header->size())
    {
        // If no features are added in the whole set, reject this request
        return buffer.free(set_header->wTotalLength);
    }
}

void descriptors::control_setup_request(device& dev, message& msg)
{
    using namespace usb::microsoft::control;

    switch (msg.request())
    {
    case GET_DESCRIPTOR:
        // assert(msg.request().wIndex == 0x0007);
        get_msos2_descriptor(dev, msg.buffer());
        if (msg.buffer().used_length() > 0)
        {
            return msg.send_buffer();
        }
        break;
    default:
        break;
    }
    return msg.reject();
}

usb::microsoft::platform_descriptor* descriptors::get_platform_descriptor(device& dev,
                                                                          df::buffer& buffer)
{
    // the only way to get the MSOS descriptor total length is to assemble it temporarily
    size_t offset = buffer.used_length();
    get_msos2_descriptor(dev, buffer);
    size_t msos_desc_size = buffer.used_length() - offset;
    buffer.free(msos_desc_size);

    auto* platform_desc = buffer.allocate<usb::microsoft::platform_descriptor>();
    platform_desc->CapabilityData.wMSOSDescriptorSetTotalLength = msos_desc_size;
    return platform_desc;
}

unsigned descriptors::bos_capabilities(device& dev, df::buffer& buffer)
{
    get_platform_descriptor(dev, buffer);
    return 1;
}

void alternate_enumeration_base::control_setup_request(device& dev, message& msg)
{
    using namespace usb::microsoft::control;

    switch (msg.request())
    {
    case SET_ALT_ENUM:
        // assert(msg.request().wIndex == 0x0008);
        using_alt_enum_ = msg.request().wValue.high_byte();
        return msg.confirm();

    default:
        return descriptors::control_setup_request(dev, msg);
    }
}

unsigned alternate_enumeration_base::bos_capabilities(device& dev, df::buffer& buffer)
{
    auto* platform_desc = get_platform_descriptor(dev, buffer);
    platform_desc->CapabilityData.bAltEnumCode = not alt_configs_by_speed(dev.bus_speed()).empty();
    return 1;
}

void alternate_enumeration_base::assign_istrings([[maybe_unused]] device& dev, istring* index)
{
    const auto ss = speeds();
    for (speed s : ss)
    {
        alt_configs_by_speed(s).for_all(&function::free_string_index);
    }
    // first allocate indexes for the configurations
    (*index) += max_config_count_ * ss.count();
    // next allocate indexes for the functions
    for (speed s : ss)
    {
        alt_configs_by_speed(s).for_all(&function::allocate_string_index, index);
    }
}

bool alternate_enumeration_base::send_owned_string([[maybe_unused]] device& dev, istring index,
                                                   string_message& smsg)
{
    const auto ss = speeds();
    // first try to match the index to a configuration
    uint8_t config_index = index - 1;
    if (config_index < (max_config_count_ * ss.count()))
    {
        speed s = speeds().at(config_index / max_config_count_);
        auto config = alt_configs_by_speed(s)[config_index % max_config_count_];
        if (config.info().name() != nullptr)
        {
            smsg.send_string(config.info().name());
        }
        else
        {
            smsg.reject();
        }
        return true;
    }
    // otherwise to a function
    for (speed s : ss)
    {
        if (alt_configs_by_speed(s).until_any(&function::send_owned_string, index, smsg))
        {
            return true;
        }
    }
    return false;
}

} // namespace usb::df::microsoft
