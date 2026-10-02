// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "usb/base.hpp"
#include "usb/standard/descriptors.hpp"

namespace usb::standard
{
/// @brief  The descriptor_set class allows iteration over USB standard descriptors contained within
///         a binary data set.
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
            if ((ptr->bLength < sizeof(T)) or (ptr->bLength > remaining_.size()))
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
            if (remaining_.size() < sizeof(descriptor_header))
            {
                return false;
            }
            const auto* desc_header = header();
            return desc_header != nullptr and desc_header->bLength >= sizeof(descriptor_header) and
                   desc_header->bLength <= remaining_.size();
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

/// @brief  The bos_capability_set class allows iteration over USB BOS capability descriptors
///         contained within a binary BOS descriptor set.
class bos_capability_set
{
  public:
    class iterator
    {
      public:
        iterator(const std::span<const uint8_t>& data, uint8_t count)
            : iterator_(data), remaining_count_(count)
        {}

        iterator& operator++()
        {
            if (remaining_count_ > 0)
            {
                ++iterator_;
                --remaining_count_;
            }
            return *this;
        }

        template <typename T>
        [[nodiscard]] const T* as() const
        {
            return valid() ? iterator_.as<T>() : nullptr;
        }

        [[nodiscard]] const uint8_t* data() const { return iterator_.data(); }
        [[nodiscard]] const usb::descriptor_header* header() const { return iterator_.header(); }

        [[nodiscard]] bool operator==(const iterator& rhs) const
        {
            return remaining_count_ == rhs.remaining_count_ and iterator_ == rhs.iterator_;
        }

        [[nodiscard]] bool valid() const
        {
            const auto* desc_header = iterator_.header();
            return (remaining_count_ > 0) and iterator_.valid() and
                   (desc_header->bLength >= sizeof(usb::descriptor_header) + sizeof(uint8_t)) and
                   (desc_header->bDescriptorType == uint8_t(descriptor::type::DEVICE_CAPABILITY));
        }

      private:
        descriptor_set::iterator iterator_;
        uint8_t remaining_count_{};
    };

    constexpr bos_capability_set() = default;
    explicit bos_capability_set(std::span<const uint8_t> bos_data)
        : bos_data_(bos_data)
    {
        if (bos_data_.size() < sizeof(descriptor::binary_object_store))
        {
            return;
        }

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        const auto* bos =
            reinterpret_cast<const descriptor::binary_object_store*>(bos_data_.data());
        if ((bos->bLength != sizeof(descriptor::binary_object_store)) or (not bos->type_valid()) or
            (bos->wTotalLength != bos_data_.size()))
        {
            return;
        }

        capabilities_ = bos_data_.subspan(sizeof(descriptor::binary_object_store));
        capability_count_ = bos->bNumDeviceCaps;
        header_valid_ = true;
    }

    [[nodiscard]] std::span<const uint8_t> data() const { return capabilities_; }
    [[nodiscard]] uint8_t size() const { return capability_count_; }

    [[nodiscard]] iterator begin() const { return iterator(capabilities_, capability_count_); }
    [[nodiscard]] iterator end() const
    {
        return iterator(capabilities_.subspan(capabilities_.size()), 0);
    }

    [[nodiscard]] bool valid() const
    {
        if (not header_valid_)
        {
            return false;
        }
        for (auto it = begin(); it != end(); ++it)
        {
            if (not it.valid())
            {
                return false;
            }
        }
        return true;
    }

  private:
    std::span<const uint8_t> bos_data_{};
    std::span<const uint8_t> capabilities_{};
    uint8_t capability_count_{};
    bool header_valid_{};
};

} // namespace usb::standard
