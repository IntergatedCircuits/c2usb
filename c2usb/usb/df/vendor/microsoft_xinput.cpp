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
#include "usb/df/vendor/microsoft_xinput.hpp"
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

    buffer.allocate<xusb::descriptor>(iface.endpoints()[0].address(),
                                      iface.endpoints()[1].address());

    iface_desc->bInterfaceNumber = if_index;
    iface_desc->bInterfaceClass = xusb::CLASS_CODE;
    iface_desc->bInterfaceSubClass = xusb::SUBCLASS_CODE;
    iface_desc->bInterfaceProtocol = xusb::PROTOCOL_CODE;
    iface_desc->iInterface = to_istring(0);
    iface_desc->bNumEndpoints = describe_endpoints(iface, buffer);
    assert((iface_desc->bNumEndpoints == 2) and
           (iface.endpoints()[0].address().direction() == direction::IN) and
           (iface.endpoints()[1].address().direction() == direction::OUT));
}

void xfunction::enable(const config::interface& iface, [[maybe_unused]] uint8_t alt_sel)
{
    app_base_function::start(iface, PROTOCOL);
}

df::config::elements<3> xconfig(xfunction& fn, const df::config::endpoint& in_ep,
                                const df::config::endpoint& out_ep)
{
    assert((in_ep.address().direction() == direction::IN) and
           (out_ep.address().direction() == direction::OUT));
    return config::to_elements({df::config::interface{fn}, in_ep, out_ep});
}

df::config::elements<3> xconfig(xfunction& fn, endpoint::address in_addr, uint8_t in_interval,
                                endpoint::address out_addr, uint8_t out_interval)
{
    return xconfig(fn,
                   standard::descriptor::endpoint::interrupt(in_addr, xusb::MAX_INPUT_REPORT_SIZE,
                                                             in_interval),
                   standard::descriptor::endpoint::interrupt(out_addr, xusb::MAX_OUTPUT_REPORT_SIZE,
                                                             out_interval));
}

} // namespace usb::df::microsoft
