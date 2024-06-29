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
#ifndef __I2C_BASE_HPP_
#define __I2C_BASE_HPP_

#include "c2usb.hpp"

namespace i2c
{
using namespace c2usb;

enum class direction : uint8_t
{
    WRITE = 0, /// The master sends data to the receiver slave
    READ = 1,  /// The master receives data from the sender slave
};

/// @brief  I2C slave address type
class address
{
  public:
    enum class mode : uint16_t
    {
        _7BIT = 0,
        _10BIT = 0x7800,
    };

    constexpr address(uint16_t code, mode m = mode::_7BIT)
        : _code((code & code_mask(m)) | static_cast<uint16_t>(m))
    {}
    bool is_10bit() const { return (_code & MODE_MASK) == static_cast<uint16_t>(mode::_10BIT); }
    uint16_t raw() const { return _code; }

    constexpr bool operator==(const address& rhs) const = default;

    // special reserved addresses
    constexpr static address general_call() { return address(0); }
    constexpr static address start_byte() { return address(1); }
    constexpr static address cbus() { return address(2); }

  private:
    static constexpr uint16_t code_mask(mode m) { return m == mode::_7BIT ? 0x7f : 0x3ff; }
    static constexpr uint16_t MODE_MASK = 0x7c00;
    const uint16_t _code;
};
} // namespace i2c

#endif // __I2C_BASE_HPP_
