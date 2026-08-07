#include <boost/ut.hpp>

#define SUITE(name) const ::boost::ut::suite<#name> name = []
#define TEST_CASE(name) ::boost::ut::detail::test{#name, (name)} = [=]() mutable
#define CHECK(...) ::boost::ut::expect(::boost::ut::that % __VA_ARGS__)
