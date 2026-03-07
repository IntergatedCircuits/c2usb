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
#include "usb/df/class/cdc.hpp"
#include "usb/class/cdc.hpp"
#include "usb/df/message.hpp"
#include "usb/standard/descriptors.hpp"

using namespace usb::cdc;
using namespace usb;

namespace usb::df::cdc
{
standard::descriptor::interface*
function::get_base_functional_descriptors(class_info cinfo, uint8_t if_index, df::buffer& buffer)
{
    struct desc_set
    {
        standard::descriptor::interface_association if_assoc{};
        standard::descriptor::interface iface{};
        usb::cdc::descriptor::header header{};
        usb::cdc::descriptor::union_<1> union_;

        desc_set(class_info cinfo, uint8_t first_if)
            : union_(first_if)
        {
            if_assoc.bFirstInterface = iface.bInterfaceNumber = first_if;
            if_assoc.bInterfaceCount = union_.interface_count();
            &if_assoc << cinfo;
            &iface << cinfo;
        }
    };
    auto* descs = buffer.allocate<desc_set>(cinfo, if_index);

    descs->if_assoc.iFunction = descs->iface.iInterface = to_istring(0);

    return &descs->iface;
}

} // namespace usb::df::cdc
