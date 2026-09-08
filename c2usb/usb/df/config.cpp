// SPDX-License-Identifier: MPL-2.0
#include "usb/df/config.hpp"

using namespace usb::df;

// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
namespace usb::df::config
{
interface_endpoint_view interface::endpoints() const
{
    return {*this};
}

interface_endpoint_view::reference interface_endpoint_view::operator[](size_t n) const
{
    assert(n < size());
    return *safe_ptr(n + 1);
}

const config::interface& endpoint::interface() const
{
    const auto* itfptr = reinterpret_cast<const config::interface*>(this);
    do // NOLINT(cppcoreguidelines-avoid-do-while)
    {
        itfptr--; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    } while (not itfptr->valid());
    return *itfptr;
}

interface_view::reference interface_view::operator[](size_t n) const
{
    for (const auto& iface : *this)
    {
        if (n == 0)
        {
            return iface;
        }
        n--;
    }
    return *reinterpret_cast<pointer>(&footer());
}

const interface_view& view::interfaces() const
{
    return reinterpret_cast<const interface_view&>(*this);
}

const endpoint_view& view::endpoints() const
{
    return reinterpret_cast<const endpoint_view&>(*this);
}

const active_endpoint_view& view::active_endpoints() const
{
    return reinterpret_cast<const active_endpoint_view&>(*this);
}
// NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

} // namespace usb::df::config
