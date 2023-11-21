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
#include "usb/standard/descriptors.hpp"
#include "usb/standard/requests.hpp"

using namespace usb::df;

uint8_t* buffer::allocate(size_type size)
{
    // need to increase CONTROL_BUFFER_SIZE
    assert((data() != nullptr) and ((used_length() + size) <= max_size()));
    auto* ptr = data() + used_length();
    used_length_ += size;
    return ptr;
}

void buffer::free(size_type size)
{
    assert(size <= used_length_);
    used_length_ -= size;
}

bool string_message::pending_reply() const
{
    return (stage_ & stages::PENDING_FLAG) != 0;
}

void string_message::setup_reply(const transfer& t)
{
    if (stage_ == stages::SETUP_PENDING)
    {
        auto size = std::min(transfer::size_type(request().wLength), t.size());
        stage_ = (t.size() > 0) ? stages::DATA : stages::STATUS;
        control_reply(request().direction(), transfer(t.data(), size));
    }
}

void string_message::set_reply(const transfer& t)
{
    if (not pending_reply())
    {
        return;
    }
    direction dir = direction::IN;
    if (stage_ != stages::SETUP_PENDING)
    {
        dir = opposite_direction(request().direction());
    }
    stage_ = stages::STATUS;
    control_reply(dir, t);
}

void string_message::reject()
{
    set_reply(transfer::stall());
}

void string_message::send_buffer()
{
    assert(request().direction() == direction::IN);
    setup_reply(transfer(buffer_));
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
    for (auto& byte : data)
    {
        string_desc->Data[offset++] = convert(byte >> 4);
        string_desc->Data[offset++] = convert(byte & 0xF);
    }

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

void message::set_control_buffer(const std::span<uint8_t>& buffer)
{
    *static_cast<transfer*>(&buffer_) = transfer(buffer);
    buffer_.clear();
}

void message::send_data(const std::span<const uint8_t>& data)
{
    assert(request().direction() == direction::IN);
    setup_reply(data);
}

void message::receive_data(const std::span<uint8_t>& data)
{
    assert(request().direction() == direction::OUT);
    setup_reply(data);
}

void message::receive_to_buffer()
{
    receive_data(std::span<uint8_t>(
        buffer().begin(), std::min(buffer::size_type(request().wLength), buffer().max_size())));
}

void message::enter_setup()
{
    stage_ = stages::SETUP_PENDING;
    buffer_.clear();
    data_ = {};
}

void message::enter_data(const transfer& t)
{
    stage_ = stages::DATA_PENDING;
    data_ = t;
}

void message::enter_status()
{
    stage_ = stages::RESET;
    data_ = {};
}
