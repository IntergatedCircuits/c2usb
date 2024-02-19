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
#ifndef __USB_DF_MESSAGE_HPP_
#define __USB_DF_MESSAGE_HPP_

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
/// @brief  The buffer allows incremental, distributed construction of control messages.
class buffer : private df::transfer
{
  public:
    using size_type = df::transfer::size_type;

    constexpr buffer() = default;

    size_type max_size() const { return df::transfer::size(); }
    size_type used_length() const { return used_length_; }
    bool empty() const { return used_length() == 0; }
    uint8_t* begin() const { return data(); }
    uint8_t* end() const { return data() + used_length(); }

    void clear() { used_length_ = 0; }
    uint8_t* allocate(size_type size);
    void free(size_type size);

    template <typename T, typename... Args>
    T* allocate(Args&&... args)
    {
        return new (allocate(sizeof(T))) T(args...);
    }

    template <typename T>
    void append(const T& data)
        requires(std::is_trivially_copyable_v<T>)
    {
        std::memcpy(allocate(sizeof(data)), &data, sizeof(data));
    }

  private:
    buffer(const buffer&) = delete;
    buffer& operator=(const buffer&) = delete;
    friend class message;
    buffer(uint8_t* data, size_type size)
        : df::transfer(data, size)
    {}

    size_type used_length_{};
};

/// @brief  The string_message class provides interface to reply string (descriptor) messages
/// to the USB host.
class string_message : public polymorphic
{
  public:
    const auto& request() const { return request_; }
    istring index() const { return request().wValue.low_byte(); }
    uint16_t language_id() const { return request().wIndex; }

    bool pending_reply() const;

    void reject();

    template <typename T>
    void send_string(std::basic_string_view<T> str)
        requires(std::is_same_v<T, char> or std::is_same_v<T, char16_t>)
    {
        auto trimmed_size = str.size();
        auto* string_desc = safe_allocate(trimmed_size);
        str = str.substr(0, trimmed_size);
        std::copy(str.begin(), str.end(), string_desc->Data);
        return send_buffer();
    }
    template <typename T>
    void send_string(const T* str);

    void send_as_hex_string(std::span<const uint8_t> data);

  protected:
    constexpr string_message() = default;

    constexpr auto stage() const { return static_cast<usb::control::stage>(stage_ >> 1); }
    enum stages : uint8_t
    {
        RESET = 0,
        PENDING_FLAG = 1,
        SETUP_PENDING = 3,
        DATA = 4,
        DATA_PENDING = 5,
        STATUS = 6,
    };

    void set_reply(const transfer& t);
    void send_buffer();
    void setup_reply(const transfer& t);

    virtual void control_reply(direction dir, const transfer& t) = 0;

    C2USB_USB_TRANSFER_ALIGN(control::request, request_){};
    df::buffer buffer_{};
    transfer data_{};
    stages stage_{};

  private:
    standard::descriptor::string* safe_allocate(size_t& size, size_t char_ratio = 1);
};

/// @brief  The message class provides interface to exchange control messages with the USB host.
class message : private string_message
{
  public:
    string_message& to_string_message() { return *(this); }

    using string_message::pending_reply;
    using string_message::request;
    using string_message::stage;

    const transfer& transferred_data() { return data_; }
    df::buffer& buffer() { return buffer_; }

    using string_message::reject;
    void confirm();
    void set_reply(bool accept);

    template <typename T>
    void send_value(T value)
        requires(std::is_convertible_v<T, sized_unsigned_t<sizeof(T)>>)
    {
        using integral_type = sized_unsigned_t<sizeof(T)>;
        assert(buffer().empty() and (request().direction() == direction::IN));
        buffer().append(etl::unaligned_type<integral_type, etl::endian::little>(
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
        requires(std::is_trivially_copyable_v<T>)
    {
        send_data(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data), sizeof(data)));
    }

    using string_message::send_buffer;

    void receive_data(const std::span<uint8_t>& data);

    template <typename T>
    void receive(T& data)
        requires(std::is_trivially_copyable_v<T>)
    {
        receive_data(std::span<uint8_t>(reinterpret_cast<uint8_t*>(&data), sizeof(data)));
    }

    void receive_to_buffer();

  protected:
    using string_message::control_reply;
    using string_message::request_;
    using string_message::string_message;
    void set_control_buffer(const std::span<uint8_t>& buffer);

    void enter_setup();
    void enter_data(const transfer& t);
    void enter_status();
};
} // namespace usb::df

#endif // __USB_DF_MESSAGE_HPP_
