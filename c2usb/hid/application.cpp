#include "hid/application.hpp"
#include "hid/transport.hpp"

namespace hid
{
c2usb::result session::send_report(const std::span<const uint8_t>& data)
{
    if (auto tp = tp_.load())
    {
        return tp->send_report(*this, data);
    }
    else
    {
        return std::errc::connection_reset;
    }
}

c2usb::result session::receive_report(const std::span<uint8_t>& data, report::type type)
{
    assert(data.size() > 0);
    if (auto tp = tp_.load())
    {
        return tp->receive_report(*this, data, type);
    }
    else
    {
        return std::errc::connection_reset;
    }
}

} // namespace hid
