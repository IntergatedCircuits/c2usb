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
#ifndef __USB_DF_CONFIG_HPP_
#define __USB_DF_CONFIG_HPP_

#include <cstring>
#include "usb/standard/descriptors.hpp"
#include "reference_array_view.hpp"

namespace usb::df
{
    class function;
}

namespace usb::df::config
{
    /// @brief  Contains all power-related configuration data.
    class power
    {
    public:
        using source = usb::power::source;

        constexpr static power bus(uint16_t max_current_mA = 100, bool remote_wakeup = false)
        {
            return power(source::BUS, max_current_mA, remote_wakeup);
        }
        constexpr static power self(bool remote_wakeup = false)
        {
            return power(source::DEVICE, 0, remote_wakeup);
        }
        constexpr static power shared(uint16_t max_current_mA, bool remote_wakeup = false)
        {
            return power(source::DEVICE, max_current_mA, remote_wakeup);
        }

        constexpr auto power_source()  const { return static_cast<usb::power::source>((value_ >> 6) & 1); }
        constexpr bool self_powered()  const { return static_cast<bool>(power_source()); }
        constexpr auto remote_wakeup() const { return static_cast<bool>((value_ >> 5) & 1); }
        constexpr auto max_power_mA()  const { return value_ >> 7; }
        constexpr bool valid()         const { return value_ != 0; }

        friend standard::descriptor::configuration* operator<<(standard::descriptor::configuration* desc, const power& p);

    protected:
        constexpr power(source src = source::BUS, uint16_t max_current_mA = 100, bool remote_wakeup = false)
                : value_((static_cast<uint16_t>(remote_wakeup) << 5) |
                        (static_cast<uint16_t>(src) << 6) | (max_current_mA << 7))
        {}
    private:
        uint16_t value_;
    };

    /// @brief  Stores the global information of a configuration.
    class header : public power
    {
    public:
        constexpr header(const power& p, const char_t* name = {})
                : power(p), name_(name)
        {}
        constexpr const char_t* name()  const { return name_; }
        constexpr uint8_t config_size() const { return config_size_; }

    private:
        friend struct detail;

        constexpr void set_size(uint8_t size) { config_size_ = size; }
        constexpr header(const header&) = default;
        constexpr header& operator=(const header&) = default;

        uint8_t config_size_ {};
        uint8_t reserved_ {};
        const char_t* name_ {};
    };

    class interface_endpoint_view;

    /// @brief Stores information of an interface in a configuration.
    class interface
    {
    public:
        constexpr interface(df::function& func, uint8_t function_index = 0, 
            uint8_t alt_settings = 0, uint8_t variant = 0)
                : alt_settings_(alt_settings), function_index_(function_index),
                  variant_(variant), function_(func)
        {}
        constexpr bool valid() const
        {
            return (always_zero_ == 0) and (std::launder(&function_) != nullptr);
        }
        constexpr df::function& function()      const { return function_; }
        constexpr uint8_t function_index()      const { return function_index_; }
        constexpr bool primary()                const { return function_index() == 0; }
        constexpr uint8_t alt_setting_count()   const { return alt_settings_ + 1; }
        constexpr uint8_t variant()             const { return variant_; }
        // only works if the interface is used through the make_config() created object
        interface_endpoint_view endpoints() const;
    private:
        interface(const interface&) = delete;
        interface& operator=(const interface&) = delete;

        const uint8_t always_zero_ {};
        uint8_t alt_settings_ {};
        uint8_t function_index_ {};
        uint8_t variant_ {};
        df::function& function_;
    };
    static_assert(sizeof(header) == sizeof(interface));

    /// @brief Stores information of an endpoint in a configuration.
    class endpoint : public standard::descriptor::endpoint
    {
    public:
        using index = uint8_t;

        constexpr endpoint(const standard::descriptor::endpoint& desc, bool unused = false)
                : standard::descriptor::endpoint(desc), unused_(unused)
        {}
        constexpr bool valid()  const { return (bLength > 0); }
        constexpr bool unused() const { return unused_; }
        // only works if the interface is used through the make_config() created object
        const config::interface& interface() const;
    private:
        endpoint(const endpoint&) = delete;
        endpoint& operator=(const endpoint&) = delete;

        bool unused_ {};
    };
    static_assert(sizeof(header) == sizeof(endpoint));

    /// @brief  Common storage type for configuration elements.
    class element
    {
    public:
        constexpr element()
                : raw_()
        {}
#ifdef __cpp_lib_bit_cast
        constexpr element(const header& h)
                : raw_(std::bit_cast<decltype(raw_)>(h))
        {}
#else
        element(const header& h)
                : element(&h)
        {}
#endif
        element(const interface& i)
                : element(&i)
        {}
        element(const endpoint& e)
                : element(&e)
        {}
        constexpr static bool is_header(const header& c)       { return false; }
        constexpr static bool is_interface(const interface& c) { return c.valid(); }
        constexpr static bool is_endpoint(const endpoint& c)   { return c.valid(); }
        constexpr bool is_footer() const { return *this == element(); }
        constexpr bool operator==(const element&) const = default;
    private:
        std::array<uint32_t, sizeof(header) / sizeof(uint32_t)> raw_;
        template <class T>
        element(const T* in)
                : raw_(*reinterpret_cast<const decltype(raw_)*>(in))
        {
            static_assert(sizeof(T) == sizeof(raw_));
        }
    };
    static_assert(sizeof(header) == sizeof(element));

    /// @brief Terminating element of a configuration.
    C2USB_STATIC_CONSTEXPR inline const element& footer()
    {
        static const element cf;
        return cf;
    }

    template <size_t SIZE>
    using elements = std::array<element, SIZE>;

    template <size_t SIZE>
    constexpr inline auto to_elements(element (&&a)[SIZE])
    {
        return std::to_array<element>(std::move(a));
    }

    struct detail
    {
        constexpr static void assign_element_array(const header&& info, const uint8_t* chunk_sizes,
                const element** chunks, element* out)
        {
            element* begin = out;
            out++;
            // add the {interface, endpoint} chunks
            while (*chunk_sizes != 0)
            {
                for (uint8_t i = 0; i < *chunk_sizes; i++)
                {
                    *out = (*chunks)[i];
                    out++;
                }
                chunks++;
                chunk_sizes++;
            }

            // first element is the info header
            auto inf = info;
            inf.set_size(std::distance(begin, out));
            *begin = inf;
            // finally, a terminating footer
            *out = {};
        }
    private:
        detail() = default;
    };

    /// @brief Creates the configuration array from the input header and list of elements.
    /// @tparam ...SIZES: deduced template parameter
    /// @param info: the configuration's base information
    /// @param ...chunks: the parts of the configuration (@ref interface s and @ref endpoint s)
    ///                   bound to sub-arrays by calling @ref std::to_array<element>(...)
    /// @return The finished configuration array that can be used via @ref view
    template <size_t... SIZES>
    constexpr elements<1 + (SIZES + ...) + 1> make_config(const header&& info, elements<SIZES>... chunks)
        requires((1 + (SIZES + ...) + 1) <= std::numeric_limits<uint8_t>::max())
    {
        constexpr uint8_t array_count = sizeof...(chunks);
        constexpr std::array<uint8_t, array_count + 1> array_lengths = { chunks.size()..., 0 };
        std::array<const element*, array_count> arrays = { &chunks[0]... };

        elements<1 + (SIZES + ...) + 1> final_array;
        detail::assign_element_array(std::move(info), array_lengths.data(), arrays.data(), final_array.data());
        return final_array;
    }

    template <class T>
    using valid_test_method = bool(*)(const T&);

    /// @brief  View type that allows iterating through the different configuration elements.
    template<class T, bool(*valid_test)(const T&), bool CONTINUE_ON_INVALID = true>
    class view_base
    {
    public:
        using value_type        = T;
        using pointer           = const T*;
        using reference         = const T&;
        using const_pointer     = const T*;
        using const_reference   = const T&;

        class reverse_view
        {
        public:
            class iterator
            {
            public:
                using iterator_category = std::input_iterator_tag;
                using difference_type = std::ptrdiff_t;

                iterator(pointer data, pointer begin)
                        : ptr_(data), begin_(begin)
                {
                    if (skip_position())
                    {
                        (*this)++;
                    }
                }
                iterator& operator++()
                {
                    do
                    {
                        ptr_--;
                    } while (skip_position());
                    return *this;
                }
                iterator operator++(int)
                {
                    iterator retval = *this;
                    ++(*this);
                    return retval;
                }
                reference operator*() { return *(ptr_); }
                pointer operator->()  { return  (ptr_); }
                bool operator==(const iterator& rhs) const
                {
                    return (ptr_ == rhs.ptr_) or (is_footer() and (rhs.is_footer()));
                }

            private:
                bool valid() const
                {
                    return valid_test(*ptr_);
                }
                constexpr bool is_footer() const
                {
                    return reinterpret_cast<const element*>(ptr_)->is_footer();
                }
                bool skip_position() const
                {
                    return not valid() and (ptr_ > begin_);
                }

                pointer ptr_;
                pointer const begin_;
            };

            using const_iterator = const iterator;

            const_iterator begin() const
            {
                return const_iterator(ptr() + size(), ptr());
            }
            const_iterator end() const
            {
                return const_iterator(ptr(), ptr());
            }
            size_t size() const
            {
                return reinterpret_cast<const config::header*>(ptr())->config_size();
            }

        private:
            friend class view_base;
            constexpr reverse_view(const_pointer ptr)
                : ptr_(ptr)
            {}

            const_pointer ptr() const
            {
                return ptr_;
            }
            const_pointer ptr_;
        };

        class iterator
        {
        public:
            using iterator_category = std::input_iterator_tag;
            using difference_type = std::ptrdiff_t;

            iterator(pointer data)
                    : ptr_(data)
            {
                if (skip_position())
                {
                    (*this)++;
                }
            }
            iterator& operator++()
            {
                if constexpr (CONTINUE_ON_INVALID)
                {
                    do
                    {
                        ptr_++;
                    } while (skip_position());
                }
                else
                {
                    ptr_++;
                    if (not valid())
                    {
                        ptr_ = reinterpret_cast<pointer>(&footer());
                    }
                }
                return *this;
            }
            iterator operator++(int)
            {
                iterator retval = *this;
                ++(*this);
                return retval;
            }
            reference operator*() { return *ptr_; }
            pointer operator->()  { return  ptr_; }
            bool operator==(const iterator& rhs) const
            {
                return (ptr_ == rhs.ptr_) or (is_footer() and (rhs.is_footer()));
            }

        private:
            bool valid() const
            {
                return valid_test(*ptr_);
            }
            bool is_footer() const
            {
                return reinterpret_cast<const element*>(ptr_)->is_footer();
            }
            bool skip_position() const
            {
                return not valid() and not is_footer();
            }

            pointer ptr_;
        };

        using const_iterator = const iterator;

        const_iterator begin() const
        {
            return safe_ptr(1);
        }
        const_iterator end() const
        {
            return reinterpret_cast<decltype(safe_ptr())>(&footer());
        }

        reverse_view reverse() const
            requires(CONTINUE_ON_INVALID)
        {
            return reverse_view(safe_ptr());
        }

        template <class Predicate>
        size_t count(Predicate p) const
        {
            size_t s = 0;
            for (auto& i : *this)
            {
                if (p(i))
                {
                    s++;
                }
            }
            return s;
        }
        size_t count() const
        {
            return count([](const_reference x){ return true; });
        }

        size_t size() const
        {
            if constexpr (CONTINUE_ON_INVALID)
            {
                return info().config_size();
            }
            else
            {
                return count();
            }
        }

        constexpr bool operator==(const view_base&) const = default;

    protected:
        constexpr view_base(const element* ptr)
                : ptr_(ptr)
        {}
        view_base(const reference& ref)
                : view_base(reinterpret_cast<decltype(ptr_)>(&ref))
        {}

        const config::header& info() const
            requires(CONTINUE_ON_INVALID)
        {
            return *reinterpret_cast<const config::header*>(safe_ptr());
        }

        const_pointer safe_ptr(size_t offset = 0) const
        {
            if (ptr_ != nullptr)
            {
                assert(not ptr_->is_footer());
                return reinterpret_cast<const_pointer>(ptr_ + offset);
            }
            return reinterpret_cast<const_pointer>(&footer());
        }

        const element* ptr_;
    };

    /// @brief  Allows iterating through a configuration's all interfaces.
    class interface_view : public view_base<interface, &element::is_interface, true>
    {
        using base = view_base<interface, element::is_interface, true>;
        friend class view;
        constexpr interface_view(const element& elem)
                : base(&elem)
        {}
    public:
        using pointer = base::pointer;
        using reference = base::reference;

        reference operator[](size_t n) const;
    };

    /// @brief  Allows iterating through a configuration's all endpoints.
    class endpoint_view : public view_base<endpoint, &element::is_endpoint, true>
    {
        using base = view_base<endpoint, element::is_endpoint, true>;
        friend class view;
        constexpr endpoint_view(const element& elem)
                : base(&elem)
        {}
    public:
        using iterator = base::iterator;
        using reference = base::reference;

        reference& at(usb::endpoint::address addr) const;

        endpoint::index indexof(reference& ep) const;
        reference operator[](endpoint::index n) const;

        size_t active_count() const;
    };

    /// @brief  Allows iterating through an interface's configured endpoints.
    class interface_endpoint_view : public view_base<endpoint, &element::is_endpoint, false>
    {
        using base = view_base<endpoint, &element::is_endpoint, false>;
        friend class interface;
        interface_endpoint_view(const interface& iface)
            : base(reinterpret_cast<const element*>(&iface))
        {}
    public:
        reference operator[](size_t n) const;
    };

    /// @brief  The view provides a view of a single configuration
    ///         (or indicates no configuration, when its base pointer is null).
    class view : protected view_base<header, &element::is_header, true>
    {
        using base = view_base<header, &element::is_header, true>;
    public:

        constexpr bool operator==(const view&) const = default;
        bool valid() const { return (info().config_size() > 0); }

        using base::info;
        interface_view interfaces() const;
        endpoint_view endpoints() const;

        template <size_t N>
        constexpr view(const elements<N>& config)
                : base(config.data())
        {}
        constexpr view()
                : base(nullptr)
        {}

        template <typename ... Args>
        constexpr static auto make_config_list_helper(const Args&... args)
        {
            return make_reference_array<const element>((*view(args).ptr_)...);
        }
    };

    /// @brief  Creates a reference array out of the input list of configuration views.
    /// @tparam Args: deduced
    /// @param  args: configuration views
    /// @return nullptr-terminated pointer array, used to create @ref view_list
    template <typename ... Args>
    constexpr static inline auto make_config_list(const Args&... args)
    {
        return view::make_config_list_helper(args...);
    }

    /// @brief  View over a container for collections of configurations,
    ///         that is created with @ref make_config_list
    class view_list : public reference_array_view<const element, const view>
    {
    public:
        template <std::size_t SIZE>
        constexpr view_list(const std::array<const element*, SIZE>& arr)
                : reference_array_view(arr)
        {}
        template <std::size_t SIZE>
        constexpr view_list(const std::array<view, SIZE>& arr)
                : view_list(*reinterpret_cast<const std::array<const element*, SIZE>*>(&arr))
        {}
        view_list()
                : reference_array_view()
        {}
        template <typename... Targs>
        void for_all(void(function::* method)(Targs...), Targs... args) const
        {
            for (auto c : *this)
            {
                for (auto& iface : c.interfaces())
                {
                    (iface.function().*method)(args...);
                }
            }
        }
        template <typename... Targs1, typename... Targs2>
        bool until_any(bool(function::* method)(Targs1...), Targs2&&... args) const
        {
            for (auto c : *this)
            {
                for (auto& iface : c.interfaces())
                {
                    if ((iface.function().*method)(std::forward<Targs1>(args)...))
                    {
                        return true;
                    }
                }
            }
            return false;
        }
    };
}

#endif // __USB_DF_CONFIG_HPP_
