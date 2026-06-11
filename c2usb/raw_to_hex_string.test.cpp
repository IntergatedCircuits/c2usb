#include "raw_to_hex_string.hpp"
#include "test_framework.hpp"

using namespace std::literals;

SUITE(raw_to_hex_string)
{
    TEST_CASE("empty input")
    {
        std::array<char, 8> buf{};
        std::span<const uint8_t> data{};

        auto written = c2usb::raw_to_hex_string<char>(data, std::span<char>(buf));
        CHECK(written == 0);
    };

    TEST_CASE("single byte")
    {
        std::array<char, 8> buf{};
        const std::array<uint8_t, 1> data{0xAF};

        auto written =
            c2usb::raw_to_hex_string<char>(std::span<const uint8_t>(data), std::span<char>(buf));
        CHECK(written == 2);
        std::string_view out(buf.data(), written);
        CHECK(out == "AF"sv);
    };

    TEST_CASE("multiple bytes")
    {
        std::array<char, 16> buf{};
        const std::array<uint8_t, 3> data{0x00, 0x10, 0xFF};

        auto written =
            c2usb::raw_to_hex_string<char>(std::span<const uint8_t>(data), std::span<char>(buf));
        CHECK(written == 6);
        std::string_view out(buf.data(), written);
        CHECK(out == "0010FF"sv);
    };

    TEST_CASE("buffer too small")
    {
        std::array<char, 4> buf{}; // can hold 2 bytes -> 4 hex chars
        const std::array<uint8_t, 3> data{{0x12, 0x34, 0x56}};

        auto written =
            c2usb::raw_to_hex_string<char>(std::span<const uint8_t>(data), std::span<char>(buf));
        CHECK(written == 4);
        std::string_view out(buf.data(), written);
        CHECK(out == "1234"sv);
    };
};
