// SPDX-License-Identifier: MPL-2.0
#include "hid/application.hpp"
#include "hid/transport.hpp"

namespace hid
{
c2usb::result session::send_report(const std::span<const uint8_t>& data)
{
    assert(not data.empty());
    if (auto* tp = tp_.load())
    {
        return tp->send_report(*this, data);
    }
    {
        return std::errc::connection_reset;
    }
}

c2usb::result session::receive_report(const std::span<uint8_t>& data, report::type type)
{
    assert(not data.empty());
    if (auto* tp = tp_.load())
    {
        return tp->receive_report(*this, data, type);
    }
    {
        return std::errc::connection_reset;
    }
}

} // namespace hid
