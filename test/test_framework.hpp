#include <ostream>
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

#define SUITE(name) const ::boost::ut::suite<#name> name = []
#define TEST_CASE(name) ::boost::ut::detail::test{#name, (name)} = [=]() mutable
#define CHECK(...) ::boost::ut::expect(::boost::ut::that % __VA_ARGS__)
