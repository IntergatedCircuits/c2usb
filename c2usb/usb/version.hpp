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
#ifndef __USB_VERSION_HPP_
#define __USB_VERSION_HPP_

#include "usb/base.hpp"

namespace usb
{
/// @brief  Binary coded decimal (BCD) version format
class version : public le_uint16_t
{
  public:
    constexpr version()
        : le_uint16_t()
    {}
    constexpr version(uint8_t majo, uint8_t mino)
    {
        major() = majo;
        minor() = mino;
    }
    constexpr version(uint8_t majo, uint8_t mino, uint8_t subminor)
    {
        major() = majo;
        minor() = (mino << 4) | (subminor & 0xF);
    }
    constexpr uint8_t major() const { return storage[1]; }
    constexpr uint8_t minor() const { return storage[0]; }
    constexpr uint8_t& major() { return storage[1]; }
    constexpr uint8_t& minor() { return storage[0]; }

    /// @brief Constructs the version from an inline string, which can contain [0;2] '.' characters.
    /// @param vstr: the version string to convert
    consteval version(const char* vstr)
        : version()
    {
        auto* dpoint = decpoint(vstr);
        major() = str2bcd(vstr, dpoint);
        if (*dpoint == '\0')
        {
            return;
        }
        dpoint++;
        auto* dpoint2 = decpoint(dpoint);
        minor() = str2bcd(dpoint, dpoint2);
        if (*dpoint2 == '\0')
        {
            return;
        }
        dpoint2++;
        minor() = minor() << 4 | (0xF & str2bcd(dpoint2, decpoint(dpoint2)));
    }

  private:
    constexpr static const char* decpoint(const char* str)
    {
        auto* dpoint = str;
        for (; (*dpoint != '.') and (*dpoint != '\0'); ++dpoint)
            ;
        return dpoint;
    }
    constexpr static uint8_t str2bcd(const char* begin, const char* end)
    {
        uint8_t result = 0;
        for (int decimals = 0; decimals < (end - begin); ++decimals)
        {
            auto dchar = *(end - 1 - decimals);
            if ((dchar < '0') or ('9' < dchar))
            {
                return 0;
            }
            result |= (dchar - '0') << (4 * decimals);
        }
        return result;
    }
};
} // namespace usb

#endif // __USB_VERSION_HPP_
