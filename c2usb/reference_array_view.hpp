/// @file
///
/// @author Benedek Kupper
/// @date   2023
///
/// @copyright
///         This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
///         If a copy of the MPL was not distributed with this file, You can obtain one at
///         https://mozilla.org/MPL/2.0/.
///
#ifndef __REFERENCE_ARRAY_VIEW_HPP_
#define __REFERENCE_ARRAY_VIEW_HPP_

#include <array>
#include <type_traits>

namespace c2usb
{
template <typename T>
struct array_to_ref_decay
{
  private:
    typedef typename std::remove_reference<T>::type U;

  public:
    typedef typename std::conditional<
        std::is_array<U>::value,
        typename std::add_lvalue_reference<typename std::remove_extent<U>::type>::type, T>::type
        type;
};
template <class T>
using array_to_ref_decay_t = typename array_to_ref_decay<T>::type;

/// @brief  Creates an array that stores the pointers in a nullptr terminated array.
/// @tparam T: shared type of all pointed arguments
/// @tparam Args: argument types (deduced)
/// @param  args: parameter pack of references to store
/// @return std::array of pointers, terminated by nullptr
template <typename T, typename... Args>
constexpr inline auto make_reference_array(Args&&... args)
{
    return std::array<T*, sizeof...(args) + 1>{(&array_to_ref_decay_t<Args>(args))..., nullptr};
}

class reference_array_view_base
{
  protected:
#if not C2USB_HAS_STATIC_CONSTEXPR
    static std::nullptr_t const* nullptr_ptr()
    {
        static const std::nullptr_t ptr{};
        return &ptr;
    }
#endif
    constexpr reference_array_view_base() {}
};

/// @brief Creates an iterable view of a nullptr terminated array.
/// @tparam T: the referenced type
template <typename T, typename TView = T*>
class reference_array_view : public reference_array_view_base
{
#if C2USB_HAS_STATIC_CONSTEXPR
    C2USB_STATIC_CONSTEXPR static T* const* nullptr_ptr()
    {
        static const T* ptr{};
        return &ptr;
    }
#endif
  public:
    class iterator
    {
      public:
        using iterator_category = std::input_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = T&;
        using pointer = T*;
        using reference = T&;
        using const_pointer = const T*;
        using const_reference = const T&;

        constexpr iterator(pointer const* data)
            : ptr_(data)
        {}
        constexpr iterator& operator++()
        {
            ptr_++;
            return *this;
        }
        constexpr iterator operator++(int)
        {
            iterator retval = *this;
            ++(*this);
            return retval;
        }
        constexpr auto operator*() { return (const TView&)(*ptr_); }
        constexpr auto operator->() { return &(const TView&)(*ptr_); }
        constexpr bool operator==(const iterator& rhs) const
        {
            return (ptr_ == rhs.ptr_) or ((*ptr_ == nullptr) and (*rhs.ptr_ == nullptr));
        }

      private:
        pointer const* ptr_;
    };

    using const_pointer = const T*;
    using const_reference = const T&;
    using pointer = T*;
    using reference = T&;

    template <std::size_t SIZE>
    constexpr reference_array_view(const std::array<T*, SIZE>& arr)
        : data_(arr.data())
    {}
    constexpr reference_array_view()
        : data_((decltype(data_))(nullptr_ptr()))
    {}
    constexpr iterator begin() const { return data_; }
    constexpr iterator end() const { return (decltype(data_))(nullptr_ptr()); }
    constexpr std::size_t size() const
    {
        pointer const* ptr;
        for (ptr = data_; *ptr != nullptr; ++ptr)
            ;
        return std::distance(data_, ptr);
    }
    constexpr bool empty() const { return *data_ == nullptr; }
    constexpr auto operator[](std::size_t n) const
    {
        pointer const* ptr;
        for (ptr = data_; (*ptr != nullptr) and (n > 0); ++ptr, --n)
            ;
        return (const TView&)(*ptr);
    }

  private:
    pointer const* data_;
};
} // namespace c2usb

#endif // __REFERENCE_ARRAY_VIEW_HPP_
