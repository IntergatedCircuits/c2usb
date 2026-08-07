// SPDX-License-Identifier: MPL-2.0
#include "usb/df/vendor/microsoft/xinput.hpp"
#include "usb/df/message.hpp"

using namespace ::hid;
using namespace usb::microsoft;
using namespace usb;

namespace usb::df::microsoft
{
void xfunction::describe_config(const config::interface& iface, uint8_t if_index,
                                df::buffer& buffer)
{
    auto* iface_desc = buffer.allocate<standard::descriptor::interface>();

    std::ignore = buffer.allocate<xusb::descriptor>(iface.endpoints()[0].address(),
                                                    iface.endpoints()[1].address());

    iface_desc->bInterfaceNumber = if_index;
    iface_desc->bInterfaceClass = xusb::CLASS_CODE;
    iface_desc->bInterfaceSubClass = xusb::SUBCLASS_CODE;
    iface_desc->bInterfaceProtocol = xusb::PROTOCOL_CODE;
    iface_desc->iInterface = name_istring();
    iface_desc->bNumEndpoints = describe_endpoints(iface, buffer);
    assert((iface_desc->bNumEndpoints == 2) and
           (iface.endpoints()[0].address().direction() == direction::IN) and
           (iface.endpoints()[1].address().direction() == direction::OUT));
}

void xfunction::enable(const config::interface& iface, [[maybe_unused]] uint8_t alt_sel)
{
    app_base_function::start(iface, PROTOCOL);
}

} // namespace usb::df::microsoft
