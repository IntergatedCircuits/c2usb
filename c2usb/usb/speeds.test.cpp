// SPDX-License-Identifier: MPL-2.0
#include "usb/speeds.hpp"
#include <algorithm>
#include <ranges>
#include <vector>
#include "test_framework.hpp"

using namespace usb;

static_assert(std::ranges::random_access_range<speeds>);
static_assert(std::random_access_iterator<speeds::iterator>);
static_assert(std::sized_sentinel_for<speeds::iterator, speeds::iterator>);
static_assert(std::default_initializable<speeds::iterator>);
static_assert(std::indirectly_readable<speeds::iterator>);

SUITE(usb_speeds)
{
    TEST_CASE("single speed range")
    {
        const speeds ss{speed::FULL};
        CHECK(ss.count() == 1);
        CHECK(ss.min == speed::FULL);
        CHECK(ss.max == speed::FULL);
        CHECK(std::ranges::distance(ss) == 1);
        CHECK(*ss.begin() == speed::FULL);
        CHECK(ss.includes(speed::FULL));
        CHECK(not ss.includes(speed::HIGH));
    };

    TEST_CASE("multi speed range iteration")
    {
        const speeds ss{speed::FULL, speed::HIGH};
        CHECK(ss.count() == 2);
        CHECK(std::ranges::distance(ss) == 2);

        std::vector<speed> visited;
        for (speed sp : ss)
        {
            visited.push_back(sp);
        }
        CHECK(visited.size() == 2);
        CHECK(visited[0] == speed::FULL);
        CHECK(visited[1] == speed::HIGH);
    };

    TEST_CASE("offset and at roundtrip")
    {
        const speeds ss{speed::FULL, speed::HIGH};
        CHECK(ss.offset(speed::FULL) == 0);
        CHECK(ss.offset(speed::HIGH) == 1);
        CHECK(ss.at(0) == speed::FULL);
        CHECK(ss.at(1) == speed::HIGH);
    };

    TEST_CASE("includes nested range")
    {
        const speeds outer{speed::LOW, speed::HIGH};
        const speeds inner{speed::FULL, speed::HIGH};
        CHECK(outer.includes(inner));
        CHECK(not inner.includes(outer));
    };

    TEST_CASE("default iterator equals end sentinel semantics")
    {
        const speeds::iterator default_it{};
        const bool default_ok = (default_it == speeds::iterator{});
        CHECK(default_ok);
    };

    TEST_CASE("iterator copy and advance")
    {
        const speeds ss{speed::LOW, speed::HIGH};
        auto it = ss.begin();
        auto copy = it;
        const bool copy_eq = (copy == it);
        CHECK(copy_eq);
        ++copy;
        const bool copy_advanced = (copy != it);
        CHECK(copy_advanced);
        CHECK(*copy == speed::FULL);
    };

    TEST_CASE("const iterator dereference")
    {
        const speeds ss{speed::FULL, speed::HIGH};
        const speeds::iterator it = ss.begin();
        CHECK(*it == speed::FULL);
        CHECK(*it.operator->() == speed::FULL);
    };

    TEST_CASE("random access arithmetic and subscript")
    {
        const speeds ss{speed::LOW, speed::HIGH};
        const auto begin = ss.begin();
        const auto end = ss.end();
        const auto dist = end - begin;

        CHECK(dist == 3);
        CHECK(std::distance(begin, end) == 3);
        CHECK(begin[2] == speed::HIGH);
        CHECK(*(begin + 2) == speed::HIGH);
        CHECK(*(2 + begin) == speed::HIGH);
        CHECK(*(end - 1) == speed::HIGH);

        auto it = begin;
        it += 2;
        const bool advanced_ok = (it == end - 1);
        CHECK(advanced_ok);
        it -= 2;
        const bool retreated_ok = (it == begin);
        CHECK(retreated_ok);
        --it;
        const bool decremented_ok = (it == begin - 1);
        CHECK(decremented_ok);

        const bool ordered_ok = (begin < end) and (end > begin);
        CHECK(ordered_ok);
    };

    TEST_CASE("std ranges algorithms over speeds")
    {
        const speeds ss{speed::LOW, speed::HIGH};

        const bool any_high = std::ranges::any_of(ss, [](speed sp) { return sp == speed::HIGH; });
        CHECK(any_high);

        size_t seen = 0;
        std::ranges::for_each(ss, [&](speed) { seen++; });
        CHECK(seen == ss.count());

        // flatten a range-of-ranges over speeds, as used for config/interface iteration
        auto flattened = ss |
                         std::views::transform([](speed sp) { return std::views::single(sp); }) |
                         std::views::join;
        std::vector<speed> collected;
        std::ranges::copy(flattened, std::back_inserter(collected));
        CHECK(collected.size() == ss.count());
    };
};
