/// @file
///
/// @author Benedek Kupper
/// @date   2024
///
/// @copyright
///         This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
///         If a copy of the MPL was not distributed with this file, You can obtain one at
///         https://mozilla.org/MPL/2.0/.
///
#ifndef __PORT_COMPATIBILITY_HELPER_HPP_
#define __PORT_COMPATIBILITY_HELPER_HPP_

#include "c2usb.hpp"

namespace c2usb
{
// very basic function traits
template <typename T>
struct function_traits;

template <typename TRet, typename... Args>
struct function_traits<TRet (*)(Args...)>
{
    using return_type = TRet;
    using arg_types = std::tuple<Args...>;
    static constexpr std::size_t arg_count = sizeof...(Args);
};

template <typename Func>
constexpr auto function_arg_count(Func func)
{
    return function_traits<Func>::arg_count;
}

template <typename Func, typename... Args>
auto invoke_function(Func func, Args&&... args)
{
    constexpr std::size_t func_arg_count = function_traits<Func>::arg_count;
    auto args_tuple = std::forward_as_tuple(std::forward<Args>(args)...);

    return []<typename Tuple, std::size_t... Indices>(Func func, Tuple&& args_tuple,
                                                      std::index_sequence<Indices...>)
    {
        return func(std::get<Indices>(std::forward<Tuple>(args_tuple))...);
    }(func, std::move(args_tuple), std::make_index_sequence<func_arg_count>{});
}

} // namespace c2usb

#endif // __PORT_COMPATIBILITY_HELPER_HPP_
