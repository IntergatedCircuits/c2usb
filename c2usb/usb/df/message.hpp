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
class mac;

/// @brief  The buffer allows incremental, distributed construction of control messages.
class buffer
{
  public:
    using size_type = uint16_t;

    size_type max_size() const { return size_; }
    size_type used_length() const { return used_length_; }
    bool empty() const { return used_length() == 0; }
    uint8_t* begin() const { return data_; }
    uint8_t* end() const { return data_ + used_length(); }

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

    constexpr buffer() = default;

  private:
    buffer(const buffer&) = delete;
    buffer& operator=(const buffer&) = delete;

    friend class mac;
    void assign(uint8_t* data, size_type size)
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
    const auto& request() const { return request_; }
    istring index() const { return request().wValue.low_byte(); }
    uint16_t language_id() const { return request().wIndex; }

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

    template <typename T>
    static constexpr std::size_t to_hex_string(std::span<const uint8_t> data, std::span<T> buffer)
    {
        auto trimmed_size = std::min(data.size(), buffer.size() / 2);
        data = data.subspan(0, trimmed_size);

        auto convert = [](uint8_t v)
        {
            if (v < 10)
            {
                return '0' + v;
            }
            else
            {
                return 'A' + v - 10;
            }
        };
        size_t offset = 0;
        for (auto byte : data)
        {
            buffer[offset++] = convert(byte >> 4);
            buffer[offset++] = convert(byte & 0xF);
        }
        return offset;
    }

    void send_as_hex_string(std::span<const uint8_t> data);

  protected:
    constexpr string_message() = default;

    void set_reply(const transfer& t);
    void send_buffer();

    void set_pending(const transfer& data = {});

    C2USB_USB_TRANSFER_ALIGN(control::request, request_){};
    df::buffer buffer_{};
    transfer data_{};
    bool pending_{};
    bool has_data_{};

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

    const transfer& data() { return data_; }
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

    // TODO: add API to ask for callback after data IN stage
};

} // namespace usb::df

#endif // __USB_DF_MESSAGE_HPP_
