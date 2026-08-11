// SPDX-License-Identifier: MPL-2.0
#include "usb/df/class/dfu.hpp"
#include "usb/df/message.hpp"
#include "usb/standard/descriptors.hpp"

using namespace usb::dfu;

namespace usb::df::dfu
{

void runtime_function::describe_config([[maybe_unused]] const config::interface& iface,
                                       uint8_t if_index, df::buffer& buffer)
{
    auto* iface_desc = buffer.allocate<standard::descriptor::interface>();

    iface_desc->bInterfaceNumber = if_index;
    iface_desc->bInterfaceClass = CLASS_CODE;
    iface_desc->bInterfaceSubClass = SUBCLASS_CODE;
    iface_desc->bInterfaceProtocol = static_cast<uint8_t>(mode::RUNTIME);
    iface_desc->iInterface = name_istring();

    auto* dfu_desc = buffer.allocate<usb::dfu::descriptor::functional>();
    dfu_desc->bmAttributes.will_detach = detach_timeout_.count() == 0;
    dfu_desc->wDetachTimeOut = detach_timeout_.count();
    // dfu_desc->wTransferSize = 0;
}

void runtime_function::control_setup_request(message& msg,
                                             [[maybe_unused]] const config::interface& iface)
{
    using namespace usb::dfu::control;
    switch (msg.request())
    {
    case DETACH:
        if (state_ != usb::dfu::state::APP_IDLE)
        {
            return msg.reject();
        }
        state_ = usb::dfu::state::APP_DETACH;
        assert(detach_cbk_ != nullptr);
        detach_cbk_(std::chrono::milliseconds(msg.request().wValue));
        return msg.confirm();

    case GETSTATUS:
        return msg.send(
            status{.bStatus = error::NONE, .bwPollTimeout = 0, .bState = state_, .iString = 0});

    case GETSTATE:
        return msg.send_value(state_);

    default:
        return msg.reject();
    }
}

} // namespace usb::df::dfu
