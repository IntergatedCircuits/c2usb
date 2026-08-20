// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "usb/base.hpp"

namespace usb
{
class descriptor_set
{
  public:
    constexpr descriptor_set() = default;
    constexpr descriptor_set(const std::span<const uint8_t>& data)
        : data_(data)
    {}

    [[nodiscard]] constexpr std::span<const uint8_t> data() const { return data_; }

    class iterator
    {
      public:
        constexpr iterator(const std::span<const uint8_t>& data)
            : remaining_(data)
        {}

        iterator& operator++()
        {
            remaining_ = remaining_.subspan(step());
            return *this;
        }

        template <typename T>
        [[nodiscard]] const T* as() const
        {
            if (remaining_.size() < sizeof(T))
            {
                return nullptr;
            }
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            const T* ptr = reinterpret_cast<const T*>(remaining_.data());
            if (ptr->bLength > remaining_.size())
            {
                return nullptr;
            }
            // only if T has TYPE_CODE static member
            // TODO: fix usb::standard::descriptor::endpoint to not shadow type(),
            // and use that instead
            if constexpr (requires { T::TYPE_CODE; })
            {
                if (ptr->bDescriptorType != uint8_t(T::TYPE_CODE))
                {
                    return nullptr;
                }
            }
            return ptr;
        }
        [[nodiscard]] const uint8_t* data() const { return remaining_.data(); }
        [[nodiscard]] const descriptor_header* header() const { return as<descriptor_header>(); }

        [[nodiscard]] constexpr bool operator==(const iterator& rhs) const
        {
            return remaining_.data() == rhs.remaining_.data();
        }

        [[nodiscard]] bool valid() const
        {
            return remaining_.size() >= sizeof(descriptor_header) and
                   header()->bLength <= remaining_.size();
        }

      private:
        size_t step() const
        {
            if (not valid())
            {
                return remaining_.size();
            }
            return header()->bLength;
        }

        std::span<const uint8_t> remaining_{};
    };

    [[nodiscard]] constexpr iterator begin() const { return iterator(data_); }
    [[nodiscard]] constexpr iterator end() const { return iterator(data_.subspan(data_.size())); }

  private:
    std::span<const uint8_t> data_{};
};

} // namespace usb
