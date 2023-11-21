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
#include "usb/standard/descriptors.hpp"
#include <cstring>

namespace usb
{

standard::descriptor::device* operator<<(standard::descriptor::device* desc, const class_info& ci)
{
    std::memcpy(&desc->bDeviceClass, &ci, sizeof(ci));
    return desc;
}

standard::descriptor::interface* operator<<(standard::descriptor::interface* desc,
                                            const class_info& ci)
{
    std::memcpy(&desc->bInterfaceClass, &ci, sizeof(ci));
    return desc;
}

standard::descriptor::interface_association*
operator<<(standard::descriptor::interface_association* desc, const class_info& ci)
{
    std::memcpy(&desc->bFunctionClass, &ci, sizeof(ci));
    return desc;
}

} // namespace usb
