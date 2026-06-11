#include "usb/version.hpp"
#include "test_framework.hpp"

SUITE(usb_version)
{
    TEST_CASE("version construction and accessors")
    {
        constexpr usb::version v1(2, 0x14);
        CHECK(v1.major() == 2);
        CHECK(v1.minor() == 0x14);

        constexpr usb::version v2(3, 1, 4);
        CHECK(v2.major() == 3);
        CHECK(v2.minor() == 0x14);

        constexpr usb::version v3("2.5.3");
        CHECK(v3.major() == 2);
        CHECK(v3.minor() == 0x53);

        constexpr usb::version v4("1.0");
        CHECK(v4.major() == 1);
        CHECK(v4.minor() == 0);

        constexpr usb::version v5("3");
        CHECK(v5.major() == 3);
        CHECK(v5.minor() == 0);
    };
};
