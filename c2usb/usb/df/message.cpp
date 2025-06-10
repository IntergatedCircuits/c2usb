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
#include "usb/df/message.hpp"
#include "raw_to_hex_string.hpp"
#include "usb/standard/descriptors.hpp"
#include "usb/standard/requests.hpp"

using namespace usb::df;

uint8_t* buffer::allocate(size_type size)
{
    // need to increase CONTROL_BUFFER_SIZE
    assert(data_ and ((used_length() + size) <= max_size()));
    auto* ptr = data_ + used_length();
    used_length_ += size;
    return ptr;
}

void buffer::free(size_type size)
{
    assert(size <= used_length_);
    used_length_ -= size;
}

usb::control::stage string_message::stage() const
{
    return stage_;
}

void string_message::set_pending(const transfer& data)
{
    buffer_.clear();
    pending_ = true;
    stage_ = data.empty() ? control::stage::SETUP : control::stage::DATA;
    data_ = data;
}

void string_message::set_reply(const transfer& t)
{
    assert(pending_);
    pending_ = false;
    data_ = transfer(t.data(), std::min(transfer::size_type(request().wLength), t.size()));
}

void string_message::reject()
{
    set_reply(transfer::stall());
}

void string_message::send_buffer()
{
    if (request().direction() != direction::IN) [[unlikely]]
    {
        assert(false);
        return reject();
    }
    set_reply(buffer_);
}

usb::standard::descriptor::string* string_message::safe_allocate(size_t& size, size_t char_ratio)
{
    // convert the ratio to byte size
    char_ratio *= sizeof(char16_t);
    auto max_size = (buffer_.max_size() - sizeof(usb::standard::descriptor::string)) / char_ratio;
    if (size > max_size)
    {
        // need to increase CONTROL_BUFFER_SIZE
        assert(false);
        size = max_size;
    }
    auto* buf = buffer_.allocate(sizeof(usb::standard::descriptor::string) + size * char_ratio);
    return new (buf) usb::standard::descriptor::string(buffer_.used_length());
}

template <>
void string_message::send_string(const char* str)
{
    return send_string(std::string_view(str));
}

template <>
void string_message::send_string(const char16_t* str)
{
    return send_string(std::u16string_view(str));
}

void string_message::send_as_hex_string(std::span<const uint8_t> data)
{
    auto trimmed_size = data.size();
    auto* string_desc = safe_allocate(trimmed_size, 2);
    raw_to_hex_string(data, std::span(string_desc->Data, trimmed_size * 2));
    return send_buffer();
}

void message::confirm()
{
    string_message::set_reply(transfer());
}

void message::set_reply(bool accept)
{
    string_message::set_reply(accept ? transfer() : transfer::stall());
}

void message::send_data(const std::span<const uint8_t>& data)
{
    if (request().direction() != direction::IN) [[unlikely]]
    {
        assert(false);
        return reject();
    }
    string_message::set_reply(data);
}

void message::receive_data(const std::span<uint8_t>& data)
{
    if ((request().direction() != direction::OUT) or (request().wLength == 0)) [[unlikely]]
    {
        assert(request().direction() == direction::OUT);
        return reject();
    }
    string_message::set_reply(data);
}

void message::receive_to_buffer()
{
    receive_data(std::span<uint8_t>(
        buffer().begin(), std::min(buffer::size_type(request().wLength), buffer().max_size())));
}
