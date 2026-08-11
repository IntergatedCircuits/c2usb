// SPDX-License-Identifier: MPL-2.0
#include "usb/class/dfu.hpp"
#include <magic_enum.hpp>

namespace usb::dfu
{

static C2USB_STATIC_CONSTEXPR auto& matrix()
{
    static C2USB_STATIC_CONSTEXPR const auto valid_states = std::to_array<uint16_t>({
        // DETACH
        (1 << uint8_t(state::APP_IDLE)),
        // DNLOAD
        (1 << uint8_t(state::IDLE)) | (1 << uint8_t(state::DNLOAD_IDLE)),
        // UPLOAD
        (1 << uint8_t(state::IDLE)) | (1 << uint8_t(state::UPLOAD_IDLE)),
        // GETSTATUS
        (1 << uint8_t(state::APP_IDLE)) | (1 << uint8_t(state::APP_DETACH)) |
            (1 << uint8_t(state::IDLE)) | (1 << uint8_t(state::DNLOAD_SYNC)) |
            (1 << uint8_t(state::DNLOAD_IDLE)) | (1 << uint8_t(state::MANIFEST_SYNC)) |
            (1 << uint8_t(state::UPLOAD_IDLE)) | (1 << uint8_t(state::ERROR)),
        // CLRSTATUS
        (1 << uint8_t(state::ERROR)),
        // GETSTATE
        (1 << uint8_t(state::APP_IDLE)) | (1 << uint8_t(state::APP_DETACH)) |
            (1 << uint8_t(state::IDLE)) | (1 << uint8_t(state::DNLOAD_SYNC)) |
            (1 << uint8_t(state::DNLOAD_IDLE)) | (1 << uint8_t(state::MANIFEST_SYNC)) |
            (1 << uint8_t(state::UPLOAD_IDLE)) | (1 << uint8_t(state::ERROR)),
        // ABORT
        (1 << uint8_t(state::IDLE)) | (1 << uint8_t(state::DNLOAD_SYNC)) |
            (1 << uint8_t(state::DNLOAD_IDLE)) | (1 << uint8_t(state::MANIFEST_SYNC)) |
            (1 << uint8_t(state::UPLOAD_IDLE)),
    });
    return valid_states;
}

bool valid_state_transition(state current, request req)
{
    if (magic_enum::enum_contains<request>(req))
    {
        return (matrix()[static_cast<uint8_t>(req)] & (1 << static_cast<uint8_t>(current))) != 0;
    }
    return false;
}

} // namespace usb::dfu
