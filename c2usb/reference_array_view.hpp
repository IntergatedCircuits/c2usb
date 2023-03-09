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

inline namespace utilities
{
    /// @brief  Creates an array that stores the pointers in a nullptr terminated array.
    /// @tparam T: shared type of all pointed arguments
    /// @tparam Args: argument types (deduced)
    /// @param  args: parameter pack of references to store
    /// @return std::array of pointers, terminated by nullptr
    template <typename T, typename ... Args>
    constexpr inline auto make_reference_array(Args&&... args)
    {
        return std::array<T*, sizeof...(args) + 1> { &args..., nullptr };
    }

    class reference_array_view_base
    {
    protected:
        static std::nullptr_t const* nullptr_ptr()
        {
            static const std::nullptr_t ptr {};
            return &ptr;
        }
        constexpr reference_array_view_base()
        {}
    };

    /// @brief Creates an iterable view of a nullptr terminated array.
    /// @tparam T: the referenced type
    template <typename T, typename TView = T*>
    class reference_array_view : public reference_array_view_base
    {
        static_assert(sizeof(TView) == sizeof(T*));
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
            constexpr auto operator*()
            {
                return (TView&)(*ptr_);
            }
            constexpr auto operator->()
            {
                return &(TView&)(*ptr_);
            }
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
        using const_iterator = const iterator;

        template <std::size_t SIZE>
        constexpr reference_array_view(const std::array<T*, SIZE>& arr)
                : data_(arr.data())
        {}
        constexpr reference_array_view()
                : data_((decltype(data_))(nullptr_ptr()))
        {}
        constexpr iterator begin() { return data_; }
        constexpr const_iterator begin() const { return data_; }
        constexpr iterator end()
        {
            return (decltype(data_))(nullptr_ptr());
        }
        constexpr const_iterator end() const
        {
            return (decltype(data_))(nullptr_ptr());
        }
        constexpr std::size_t size() const
        {
            pointer const* ptr;
            for (ptr = data_; *ptr != nullptr; ++ptr);
            return std::distance(data_, ptr);
        }
        constexpr bool empty() const { return *data_ == nullptr; }
        constexpr auto operator[](std::size_t n) const
        {
            pointer const* ptr;
            for (ptr = data_; (*ptr != nullptr) and (n > 0); ++ptr, --n);
            return (const TView&)(*ptr);
        }
    private:
        pointer const* data_;
    };
}

#endif // __REFERENCE_ARRAY_VIEW_HPP_
