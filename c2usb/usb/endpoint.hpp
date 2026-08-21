// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cassert>
#include <chrono>
#include "usb/base.hpp"

namespace usb::endpoint
{
enum class type : uint8_t
{
    CONTROL = 0,
    ISOCHRONOUS = 1,
    BULK = 2,
    INTERRUPT = 3
};

namespace isochronous
{
enum class sync : uint8_t
{
    NONE = 0,
    ASYNCHRONOUS = 1,
    ADAPTIVE = 2,
    SYNCHRONOUS = 3
};

enum class usage : uint8_t
{
    DATA = 0,
    FEEDBACK = 1,
    EXPLICIT_FEEDBACK_DATA = 2,
};
} // namespace isochronous

C2USB_STATIC_CONSTEXPR inline uint16_t packet_size_limit(type t, speed s)
{
    if (s == speed::NONE)
    {
        return 0;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    C2USB_STATIC_CONSTEXPR static const uint16_t sizes[4][3] = {
        // clang-format off
        // LS    FS    HS
        {  8,    64,   64 }, // CONTROL
        {  0,  1023, 1024 }, // ISOCHRONOUS
        {  0,    64,  512 }, // BULK
        {  8,    64, 1024 }  // INTERRUPT
        // clang-format on
    };
    return sizes[static_cast<uint8_t>(t)]
                [static_cast<uint8_t>(s) - static_cast<uint8_t>(speed::LOW)];
}

class address
{
  public:
    constexpr operator uint8_t() const { return value_; }
    constexpr operator uint8_t&() { return value_; }

    constexpr explicit address(uint8_t value)
        : value_(value /* & 0x8F */)
    {}
    constexpr address(usb::direction dir, uint8_t number)
        : value_((static_cast<uint8_t>(dir) << 7) | (number & 0xF))
    {}
    [[nodiscard]] constexpr uint8_t number() const { return value_ & 0xF; }

    [[nodiscard]] constexpr bool valid() const { return (value_ & 0x70) == 0; }
    [[nodiscard]] constexpr static address invalid() { return address(0x70); }
    [[nodiscard]] constexpr static address control(usb::direction dir) { return {dir, 0}; }
    [[nodiscard]] constexpr static address control_in() { return control(usb::direction::IN); }
    [[nodiscard]] constexpr static address control_out() { return control(usb::direction::OUT); }

    [[nodiscard]] constexpr usb::direction direction() const
    {
        return static_cast<usb::direction>(value_ >> 7);
    }

  private:
    uint8_t value_;
};

class interval
{
  public:
    /// @brief  Calculate an endpoint interval for full/high speed isochronous
    ///         or high speed interrupt endpoints.
    /// @param  microframes: The interval in microframes (125μs units). Must be a power of 2, and in
    ///         the range 1-32768.
    /// @return The interval value (1 to 16), or 0 if the input is invalid
    [[nodiscard]] static constexpr uint8_t from_microframes(uint16_t microframes)
    {
        if ((microframes <= 0) or (microframes & (microframes - 1)) != 0)
        {
            assert(false && "microframes must be a power of 2");
            return 0;
        }
        constexpr auto offset = std::countr_one(std::numeric_limits<decltype(microframes)>::max());
        return offset - std::countl_zero(microframes);
    }

    /// @brief  Calculate an endpoint interval for interrupt endpoints.
    /// @param  speed: The operating speed of the endpoint.
    /// @param  period: The interval in microseconds.
    /// @return The interval value, or 0 if the input is invalid
    [[nodiscard]] static constexpr uint8_t from_rate(usb::speed speed,
                                                     std::chrono::microseconds period)
    {
        switch (speed)
        {
        case usb::speed::FULL:
            // full speed frame rate is 1ms
            return static_cast<uint8_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(period).count());
        case usb::speed::HIGH:
            // high speed microframe rate is 125us
            return from_microframes(static_cast<uint16_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(period).count() / 125));
        case usb::speed::LOW:
            return std::max<uint8_t>(
                10, std::chrono::duration_cast<std::chrono::milliseconds>(period).count());
        default:
            assert(false && "invalid speed");
            return 0;
        }
    }

    /// @brief  For high speed bulk out endpoints, interval sets the maximum NAK packet limit.
    /// @param  max_nak_rate: Set to zero when the device never NAKs the endpoint.
    ///         When non-zero, device may NAK at most once per max_nak_rate microframes.
    /// @return The interval value (0 to 255)
    [[nodiscard]] static constexpr uint8_t bulk_out_nak_rate_limit(uint8_t max_nak_rate)
    {
        return max_nak_rate;
    }

    constexpr operator uint8_t() const { return value_; }

  private:
    constexpr interval(uint8_t value)
        : value_(value)
    {}
    uint8_t value_;
};

} // namespace usb::endpoint
