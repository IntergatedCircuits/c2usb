#include <ostream>
#include <tuple>
#include <boost/ut.hpp>
#include <magic_enum.hpp>

// lets boost::ut print any enum value on assertion failure, without a per-enum operator<<
// must live in the enum's own namespace, ADL won't find it here for e.g. usb::speed
namespace usb
{
template <typename T>
    requires std::is_enum_v<T>
std::ostream& operator<<(std::ostream& os, T value)
{
    return os << magic_enum::enum_name(value);
}
} // namespace usb

namespace test_framework
{
// binds the parameter types before the test body, so TEMPLATE_TEST_CASE can end with the lambda
template <class... Ts>
struct for_types
{
    template <class F>
    constexpr auto operator%(F f) const
    {
        return ::boost::ut::operators::operator|(f, std::tuple<Ts...>{});
    }
};
} // namespace test_framework

#define SUITE(name) const ::boost::ut::suite<#name> name = []
#define TEST_CASE(name) ::boost::ut::detail::test{#name, (name)} = [=]() mutable
/// @brief  Test case run once for each of the listed types, available as TestType in the body
#define TEMPLATE_TEST_CASE(name, ...)                                                              \
    ::boost::ut::detail::test{#name, (name)} =                                                     \
        ::test_framework::for_types<__VA_ARGS__>{} % [=]<class TestType>() mutable
#define CHECK(...) ::boost::ut::expect(::boost::ut::that % __VA_ARGS__)
