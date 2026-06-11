#include "reference_array_view.hpp"
#include "test_framework.hpp"
#include <string_view>

using namespace std::literals;

SUITE(reference_array_view)
{
    TEST_CASE("empty array view")
    {
        static_assert(std::ranges::range<c2usb::reference_array_view<const int>>);
        static constexpr auto arr = c2usb::make_reference_array<int>();
        static_assert(std::is_same_v<std::remove_cv_t<decltype(arr)>, std::array<int*, 1>>);
        constexpr auto view = c2usb::reference_array_view<int>(arr);
        CHECK(view.begin() == view.end());
        CHECK(view.size() == 0);
        CHECK(*view.begin() == nullptr);
        CHECK(view[0] == nullptr);
        CHECK(view[1] == nullptr);
    };

    TEST_CASE("integer array view")
    {
        static_assert(std::ranges::range<c2usb::reference_array_view<const int>>);
        static constexpr int a = 1, b = 2, c = 3;
        static constexpr auto arr = c2usb::make_reference_array<const int>(a, b, c);
        static_assert(std::is_same_v<std::remove_cv_t<decltype(arr)>, std::array<const int*, 4>>);
        constexpr auto view = c2usb::reference_array_view<const int>(arr);
        CHECK(view.size() == 3);
        CHECK(std::ranges::distance(view) == 3);
        CHECK(*view.begin() == &a);
        CHECK(view[0] == &a);
        CHECK(view[1] == &b);
        CHECK(view[2] == &c);
        CHECK(view[3] == nullptr);
    };

    TEST_CASE("string array view")
    {
        static_assert(
            std::ranges::range<c2usb::reference_array_view<const char, std::string_view>>);
        auto arr = c2usb::make_reference_array<const char>("hello", "reference", "array", "world");
        static_assert(std::is_same_v<std::remove_cv_t<decltype(arr)>, std::array<const char*, 5>>);
        auto view = c2usb::reference_array_view<const char, std::string_view>(arr);
        CHECK(view.size() == 4);
        CHECK(std::ranges::distance(view) == 4);
        CHECK(*view.begin() == "hello"sv);
        CHECK(view[0] == "hello"sv);
        CHECK(view[1] == "reference"sv);
        CHECK(view[2] == "array"sv);
        CHECK(view[3] == "world"sv);
        CHECK(view[4] == ""sv);
    };
};