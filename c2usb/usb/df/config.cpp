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
#include "usb/df/config.hpp"

using namespace usb::df;
using namespace usb::df::config;

namespace usb::df::config
{
usb::standard::descriptor::configuration* operator<<(usb::standard::descriptor::configuration* desc,
        const power& p)
{
    desc->bMaxPower = p.value_ >> 8;
    desc->bmAttributes = 0x80 | p.value_;
    return desc;
}

}

interface_endpoint_view interface::endpoints() const
{
    return interface_endpoint_view(*this);
}

interface_endpoint_view::reference interface_endpoint_view::operator[](size_t n) const
{
    assert(n < size());
    return *safe_ptr(n + 1);
}

const config::interface& endpoint::interface() const
{
    auto* itfptr = reinterpret_cast<const config::interface*>(this);
    do
    {
        itfptr--;
    } while (not itfptr->valid());
    return *itfptr;
}

interface_view::reference interface_view::operator[](size_t n) const
{
    for (auto& iface : *this)
    {
        if (n == 0)
        {
            return iface;
        }
        n--;
    }
    return *reinterpret_cast<pointer>(&footer());
}

endpoint::index endpoint_view::indexof(reference& ep) const
{
    auto ptrdiff = std::distance(reinterpret_cast<pointer>(ptr_), &ep);
    assert((0 < ptrdiff) and (ptrdiff < info().config_size()));
    return ptrdiff;
}

endpoint_view::reference endpoint_view::operator[](endpoint::index n) const
{
    assert((n < size()) and safe_ptr(n)->valid());
    return *safe_ptr(n);
}

size_t endpoint_view::active_count() const
{
    return count([](const_reference ep){ return !ep.unused(); });
}

interface_view view::interfaces() const
{
    return interface_view(*this->ptr_);
}

endpoint_view view::endpoints() const
{
    return endpoint_view(*this->ptr_);
}

endpoint_view::reference endpoint_view::at(usb::endpoint::address addr) const
{
    for (auto& ep : *this)
    {
        if (ep.address() == addr)
        {
            return ep;
        }
    }
    return *reinterpret_cast<pointer>(&footer());
}
