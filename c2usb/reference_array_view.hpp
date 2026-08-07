// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <ostream>
#include <type_traits>

namespace c2usb
{
template <typename T>
struct array_to_ref_decay
{
  private:
    using U = typename std::remove_reference_t<T>;

  public:
    using type = typename std::conditional_t<
        std::is_array_v<U>, typename std::add_lvalue_reference_t<typename std::remove_extent_t<U>>,
        T>;
};
template <class T>
using array_to_ref_decay_t = typename array_to_ref_decay<T>::type;

/// @brief  Creates an array that stores the pointers in a nullptr terminated array.
/// @tparam T: shared type of all pointed arguments
/// @tparam Args: argument types (deduced)
/// @param  args: parameter pack of references to store
/// @return std::array of pointers, terminated by nullptr
template <typename T, typename... Args>
constexpr auto make_reference_array(Args&&... args)
{
    return std::array<T*, sizeof...(args) + 1>{
        (&array_to_ref_decay_t<Args>(std::forward<Args>(args)))..., nullptr};
}

class reference_array_view_base
{
  protected:
    static std::nullptr_t const* nullptr_ptr()
    {
        static const std::nullptr_t ptr{};
        return &ptr;
    }
    constexpr reference_array_view_base() = default;
};

/// @brief Creates an iterable view of a nullptr terminated array.
/// @tparam T: the referenced type
template <typename T, typename TView = T*>
class reference_array_view : public reference_array_view_base
{
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
            ptr_++; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            return *this;
        }
        constexpr iterator operator++(int)
        {
            iterator retval = *this;
            ++(*this);
            return retval;
        }
        constexpr TView operator*() const { return (*ptr_ != nullptr) ? TView(*ptr_) : TView{}; }
        constexpr auto operator->() { return &(const TView&)(*ptr_); }
        constexpr bool operator==([[maybe_unused]] std::nullptr_t const* rhs) const
        {
            return (*ptr_ == nullptr);
        }

        friend std::ostream& operator<<(std::ostream& os, const iterator& it)
        {
            os << std::hex << std::bit_cast<std::uintptr_t>(it.ptr_) << std::dec << "\n";
            return os;
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
    [[nodiscard]] constexpr iterator begin() const { return data_; }
    [[nodiscard]] constexpr auto end() const { return nullptr_ptr(); }
    [[nodiscard]] constexpr std::size_t size() const
    {
        pointer const* ptr = data_;
        for (; *ptr != nullptr; ++ptr) // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        {
        }
        return static_cast<std::size_t>(std::distance(data_, ptr));
    }
    [[nodiscard]] constexpr bool empty() const { return *data_ == nullptr; }
    constexpr TView operator[](std::size_t n) const
    {
        auto it = begin();
        for (; (it != end()) and (n > 0); ++it, --n)
        {
        }
        return *it;
    }

  private:
    pointer const* data_;
};
} // namespace c2usb
