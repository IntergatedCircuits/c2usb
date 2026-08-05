// SPDX-License-Identifier: MPL-2.0
#include "bluetooth/hid_over_gatt_sci.hpp"
#include "test_framework.hpp"

using namespace std::chrono_literals;
using namespace bluetooth;
using namespace bluetooth::hid_over_gatt;

SUITE(hid_over_gatt_sci)
{
    TEST_CASE("round chrono durations to bluetooth units")
    {
        constexpr sci_mode_params params(sci_mode::FULL_RANGE, 380us, 440us, 1, 10, 0, 0, 104ms,
                                         188us, 313us);

        CHECK(conn_event_unit<>(params.interval_min_125us) == 375us); // 380 us rounds to 375 us
        CHECK(conn_event_unit<>(params.interval_max_125us) == 500us); // 440 us rounds to 500 us
        CHECK(supervision_timeout_unit<>(params.supervision_timeout_10ms) ==
              100ms);                                               // 104 ms rounds to 100 ms
        CHECK(conn_event_unit<>(params.min_ce_len_125us) == 250us); // 188 us rounds to 250 us
        CHECK(conn_event_unit<>(params.max_ce_len_125us) == 375us); // 313 us rounds to 375 us
    };

    TEST_CASE("shortest_supervision_timeout gives minimal valid step")
    {
        constexpr auto timeout = sci_mode_params::shortest_supervision_timeout(15ms, 4, 0);
        CHECK(timeout.count() == 13); // 130 ms

        constexpr sci_mode_params params_default(sci_mode::DEFAULT, 7500us, 15ms, 1, 4, 0, 0,
                                                 timeout, 125us, 1999875us);
        CHECK(params_default.supervision_timeout_valid());
        CHECK(params_default.latency_valid());

        auto params2 = params_default;
        params2.supervision_timeout_10ms--;
        CHECK(not params2.supervision_timeout_valid());

        constexpr auto timeout_fast = sci_mode_params::shortest_supervision_timeout(5ms, 4, 0);
        constexpr sci_mode_params params_fast(sci_mode::FAST, 1250us, 5ms, 1, 4, 0, 3, timeout_fast,
                                              125us, 1999875us);
        CHECK(params_fast.supervision_timeout_valid());
        CHECK(params_fast.latency_valid());

        params2 = params_fast;
        params2.supervision_timeout_10ms--;
        CHECK(not params2.supervision_timeout_valid());
    };

    TEST_CASE("recommended presets satisfy core constraints")
    {
        for (const auto& params : sci_mode_params::recommended_set())
        {
            CHECK(params.interval_min_125us <= params.interval_max_125us);
            CHECK(conn_event_unit<>(params.interval_min_125us) >= std::chrono::microseconds{375});
            CHECK(conn_event_unit<>(params.interval_max_125us) <= std::chrono::milliseconds{4000});
            CHECK(params.subrate_min <= params.subrate_max);
            CHECK(params.subrate_min >= 1);
            CHECK(params.subrate_max <= 500);
            CHECK(params.continuation_number < params.subrate_max);
            CHECK(params.supervision_timeout_valid());
            auto params2 = params;
            params2.supervision_timeout_10ms--;
            CHECK(not params2.supervision_timeout_valid());
            CHECK(params.latency_valid());
            CHECK(params.min_ce_len_125us <= params.max_ce_len_125us);
            CHECK(conn_event_unit<>(params.min_ce_len_125us) >= std::chrono::microseconds{125});
            CHECK(conn_event_unit<>(params.max_ce_len_125us) <=
                  std::chrono::microseconds{1'999'875});
        }
    };

    TEST_CASE("constructor without supervision timeout uses shortest value")
    {
        constexpr sci_mode_params params(sci_mode::FULL_RANGE, 1250us, 15ms, 1, 4, 0, 1);
        constexpr auto shortest = sci_mode_params::shortest_supervision_timeout(15ms, 4, 0);

        CHECK(params.supervision_timeout_10ms == shortest.count());
        CHECK(params.supervision_timeout_valid());
        CHECK(params.latency_valid());
    };
};
