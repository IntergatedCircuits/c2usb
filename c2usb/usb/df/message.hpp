// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cassert>
#include <cstring>
#include <new>
#include <string_view>

#include "sized_unsigned.hpp"
#include "usb/control.hpp"
#include "usb/df/transfer.hpp"

namespace usb::standard::descriptor
{
struct string;
}

namespace usb::df
{
class mac;

/// @brief  The buffer allows incremental, distributed construction of control messages.
class buffer
{
  public:
    using size_type = uint16_t;

    [[nodiscard]] size_type max_size() const { return size_; }
    [[nodiscard]] size_type used_length() const { return used_length_; }
    [[nodiscard]] bool empty() const { return used_length() == 0; }
    [[nodiscard]] uint8_t* begin() const { return data_; }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    [[nodiscard]] uint8_t* end() const { return data_ + used_length(); }

    void clear() { used_length_ = 0; }
    [[nodiscard]] uint8_t* allocate(size_type size);
    void free(size_type size);

    template <typename T, typename... Args>
    [[nodiscard]] T* allocate(Args&&... args)
    {
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        return new (allocate(sizeof(T))) T(std::forward<Args>(args)...);
    }

    template <typename T>
    void append(const T& data)
        requires(std::is_trivially_copyable_v<T>)
    {
        std::memcpy(allocate(sizeof(data)), &data, sizeof(data));
    }

    constexpr buffer() = default;
    buffer(const buffer&) = delete;
    buffer& operator=(const buffer&) = delete;
    buffer(buffer&&) = delete;
    buffer& operator=(buffer&&) = delete;
    ~buffer() = default;

  private:
    friend class mac;
    constexpr void assign(uint8_t* data, size_type size)
    {
        data_ = data;
        size_ = size;
        used_length_ = 0;
    }

    uint8_t* data_{};
    size_type size_{};
    size_type used_length_{};
};

/// @brief  The string_message class provides interface to reply string (descriptor) messages
/// to the USB host.
class string_message
{
  public:
    [[nodiscard]] const auto& request() const { return request_; }
    [[nodiscard]] istring index() const { return request().wValue.low_byte(); }
    [[nodiscard]] uint16_t language_id() const { return request().wIndex; }

    [[nodiscard]] control::stage stage() const { return stage_; }

    void reject();

    template <typename T>
    void send_string(const std::basic_string_view<T>& str)
        requires(std::is_same_v<T, char> or std::is_same_v<T, char16_t>)
    {
        auto trimmed_size = str.size();
        auto* string_desc = safe_allocate(trimmed_size);
        std::copy(str.begin(), str.begin() + trimmed_size, string_desc->Data);
        return send_buffer();
    }
    template <typename T>
    void send_string(const T* str);

    void send_as_hex_string(std::span<const uint8_t> data);

  protected:
    constexpr string_message() = default;

    void set_reply(const transfer& t);
    void send_buffer();

    void set_pending(const transfer& data = {});

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    C2USB_USB_TRANSFER_ALIGN(control::request, request_) {};
    df::buffer buffer_;
    transfer data_;
    bool pending_{};
    control::stage stage_{};
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)

  private:
    standard::descriptor::string* safe_allocate(size_t& size, size_t char_ratio = 1);
};

/// @brief  The message class provides interface to exchange control messages with the USB host.
class message : protected string_message
{
  public:
    using string_message::string_message;
    string_message& to_string_message() { return *(this); }

    using string_message::request;
    using string_message::stage;

    const transfer& data() { return data_; }
    df::buffer& buffer() { return buffer_; }

    using string_message::reject;
    void confirm();
    void set_reply(bool accept);

    template <typename T>
    void send_value(T value) // NOLINT(performance-unnecessary-value-param)
        requires(std::is_convertible_v<T, sized_unsigned_t<sizeof(T)>>)
    {
        using integral_type = sized_unsigned_t<sizeof(T)>;
        assert(buffer().empty() and (request().direction() == direction::IN));
        buffer().append(bitfilled::packed_integer<std::endian::little, sizeof(T)>(
            static_cast<integral_type>(value)));
        send_buffer();
    }

    template <typename T>
    void send_value(T value)
        requires(std::is_enum_v<T>)
    {
        send_value(static_cast<std::underlying_type_t<T>>(value));
    }

    void send_data(const std::span<const uint8_t>& data);

    template <typename T>
    void send(const T& data)
    {
        send_data(std::span<const uint8_t>(std_layout_cast<const uint8_t*>(&data), sizeof(data)));
    }

    using string_message::send_buffer;

    void receive_data(const std::span<uint8_t>& data);

    template <typename T>
    void receive(T& data)
    {
        receive_data(std::span<uint8_t>(std_layout_cast<uint8_t*>(&data), sizeof(data)));
    }

    void receive_to_buffer();

    // TODO: add API to ask for callback after data IN stage
};

} // namespace usb::df
