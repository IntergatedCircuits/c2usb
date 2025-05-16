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
#include <magic_enum.hpp>

#include "i2c/hid/device.hpp"

using namespace ::hid;
using namespace i2c::hid;

device::device(application& app, const product_info& pinfo, i2c::slave& slave, i2c::address address,
               uint16_t hid_descriptor_reg_address)
    : module(address),
      app_(app),
      pinfo_(pinfo),
      slave_(slave),
      hid_descriptor_reg_(hid_descriptor_reg_address)
{
    slave_.register_module(*this);
}

device::~device()
{
    // deassert int pin
    slave().set_pin_interrupt(false);

    // disable I2C
    slave().unregister_module(*this);

    // clear context
    get_report_.clear();
    in_queue_.clear();
    rx_buffers_ = {};
}

void device::link_reset()
{
    // reset application
    app_.teardown(this);

    // clear context
    get_report_.clear();
    in_queue_.clear();
    rx_buffers_ = {};

    // send 0 length input report
    queue_input_report(std::span<const uint8_t>());
}

result device::receive_report(const std::span<uint8_t>& data, report::type type)
{
    assert(type != report::type::INPUT);
    if ((rx_buffers_[type].size() == 0) or (stage_ == 0))
    {
        // save the target buffer for when the transfer is made
        rx_buffers_[type] = data;
        return result::ok;
    }
    else
    {
        // the previously passed buffer is being used for receiving data
        return result::device_or_resource_busy;
    }
}

result device::send_report(const std::span<const uint8_t>& data, report::type type)
{
    assert(type != report::type::OUTPUT);
    // if the function is invoked in the GET_REPORT callback context,
    // and the report type and ID matches, transmit immediately (without interrupt)
    if ((get_report_.type() == type) and
        ((get_report_.id() == 0) or (get_report_.id() == data.front())))
    {
        auto& report_length = *get_buffer<le_uint16_t>();
        report_length = static_cast<uint16_t>(data.size());

        // send the length 2 bytes, followed by the report data
        slave().send(&report_length, data);

        // mark completion
        get_report_.clear();

        return result::ok;
    }
    else if (type == report::type::INPUT)
    {
        return queue_input_report(data) ? result::ok : result::device_or_resource_busy;
    }
    else
    {
        // feature reports can only be sent if a GET_REPORT command is pending
        // output reports cannot be sent
        return result::invalid_argument;
    }
}

void device::get_hid_descriptor(descriptor& desc) const
{
    // fill in the descriptors with the data from different sources
    desc.reset();
    desc.set_registers<registers>();
    desc.set_protocol(app_.report_info());
    desc.set_product_info(pinfo_);
}

bool device::get_report(report::selector select)
{
    // mark which report needs to be redirected through the data register
    get_report_ = select;

    // issue request to application, let it provide report through send_report()
    app_.get_report(select, buffer_);

    // if application provided the report, the request has been cleared
    return !get_report_.valid();
}

bool device::get_command(const std::span<const uint8_t>& command_data)
{
    auto& cmd = *reinterpret_cast<const command_view*>(command_data.data());
    auto cmd_size = cmd.size();
    uint16_t data_reg = *reinterpret_cast<const le_uint16_t*>(command_data.data() + cmd_size);

    if ((command_data.size() != (cmd_size + sizeof(data_reg))) or (data_reg != registers::DATA))
    {
        // invalid size or register
        return false;
    }

    switch (cmd.opcode())
    {
    case opcode::GET_REPORT:
    {
        auto type = cmd.report_type();
        if ((type != report::type::FEATURE) and (type != report::type::INPUT))
        {
            return false;
        }
        return get_report(cmd.report_selector());
    }

#if C2USB_I2C_HID_FULL_COMMAND_SUPPORT
    case opcode::GET_IDLE:
        send_short_data(static_cast<uint16_t>(app_.get_idle(cmd.report_id())));
        return true;

    case opcode::GET_PROTOCOL:
        send_short_data(static_cast<uint16_t>(app_.get_protocol()));
        return true;
#endif
    default:
        return false;
    }
}

void device::send_short_data(uint16_t value)
{
    short_data& data = *get_buffer<short_data>();

    data.length = sizeof(data);
    data.value = value;

    slave().send(&data);
}

bool device::reply_request(size_t data_length)
{
    const std::span<const uint8_t> data{buffer_.data(), data_length};
    uint16_t reg = *reinterpret_cast<const le_uint16_t*>(data.data());

    if (data.size() == sizeof(reg))
    {
        if (reg == hid_descriptor_reg_address())
        {
            // HID descriptor tells the host the necessary parameters for communication
            descriptor& desc = *get_buffer<descriptor>();

            get_hid_descriptor(desc);
            slave().send(&desc);
            return true;
        }
        else if (reg == registers::REPORT_DESCRIPTOR)
        {
            // Report descriptor lets the host interpret the raw report data
            auto& rdesc = app_.report_info().descriptor;

            slave().send(rdesc.to_span());
            return true;
        }
        else
        {
            // invalid size or register
            return false;
        }
    }
    else if ((data.size() > sizeof(reg)) and (reg == registers::COMMAND))
    {
        return get_command(data.subspan(sizeof(reg)));
    }
    else
    {
        // invalid size or register
        return false;
    }
}

bool device::queue_input_report(const std::span<const uint8_t>& data)
{
    slave().set_pin_interrupt(true);
    return in_queue_.push(data);
}

bool device::get_input()
{
    // send the next report from the queue
    std::span<const uint8_t> input_data;
    if (in_queue_.front(input_data) and (input_data.size() > 0))
    {
        auto& report_length = *reinterpret_cast<le_uint16_t*>(buffer_.data());
        report_length = static_cast<uint16_t>(input_data.size());

        slave().send(&report_length, input_data);
    }
    else
    {
        // this is a reset, or the master is only checking our presence on the bus
        auto& reset_sentinel = *reinterpret_cast<le_uint16_t*>(buffer_.data());
        reset_sentinel = 0;

        slave().send(&reset_sentinel);
    }

    // de-assert interrupt line now, as if it's only done at stop,
    // the host will try to read another report
    slave().set_pin_interrupt(false);

    return true;
}

bool device::on_start(i2c::direction dir, size_t data_length)
{
    bool success = false;

    if (stage_ == 0)
    {
        assert(data_length == 0);

        if (dir == i2c::direction::READ)
        {
            // when no register address is sent, the host is reading an input report
            // on the request of the interrupt pin
            success = get_input();
        }
        else
        {
            // first part of the transfer, receive register / command
            stage_ = 1;
            slave().receive(buffer_, rx_buffers_.largest());
            success = true;
        }
    }
    else if (dir == i2c::direction::READ)
    {
        // successive start, we must reply to received command
        success = reply_request(data_length);
    }

    return success;
}

void device::delegate_power_event(event ev)
{
    if (power_event_delegate_)
    {
        power_event_delegate_(*this, ev);
    }
}

void device::set_power(bool powered)
{
    if (powered_ != powered)
    {
        powered_ = powered;
        delegate_power_event(event::POWER_STATE_CHANGE);
    }
}

bool device::set_report(report::type type, const std::span<const uint8_t>& data)
{
    uint16_t length = *reinterpret_cast<const le_uint16_t*>(data.data());
    size_t report_length = length - sizeof(length);
    auto& output_buffer = rx_buffers_[type];

    // check length validity
    if ((data.size() == length) and (report_length <= output_buffer.size()))
    {
        // copy/move the output report into the requested buffer
        size_t header_size = (data.data() - buffer_.data()) + sizeof(length);
        size_t offset = buffer_.size() - header_size;

        // move the output buffer contents down by offset
        if (offset < report_length)
        {
            std::move_backward(output_buffer.data(), output_buffer.data() + report_length - offset,
                               output_buffer.data() + offset);
        }

        // copy first part from GP buffer into provided buffer
        std::copy(data.begin() + sizeof(length), data.end(), output_buffer.begin());

        auto report = output_buffer.subspan(0, report_length);

        // clear the buffer before the callback, so it can set a new buffer
        output_buffer = {};

        // pass it to the app
        app_.set_report(type, report);

        return true;
    }
    else
    {
        return false;
    }
}

bool device::set_command(const std::span<const uint8_t>& command_data)
{
    const auto& cmd = *reinterpret_cast<const command_view*>(command_data.data());
    auto cmd_size = cmd.size();
    uint16_t data_reg = *reinterpret_cast<const le_uint16_t*>(command_data.data() + cmd_size);

    switch (cmd.opcode())
    {
    case opcode::RESET:
        if (command_data.size() != cmd_size)
        {
            // invalid size
            return false;
        }

        link_reset();
        return true;

    case opcode::SET_POWER:
        if (command_data.size() != cmd_size)
        {
            // invalid size
            return false;
        }

        set_power(!cmd.sleep());
        return true;

    case opcode::SET_REPORT:
    {
        auto type = cmd.report_type();
        if ((command_data.size() <= (cmd_size + sizeof(data_reg))) or
            (data_reg != registers::DATA) or
            ((type != report::type::FEATURE) and (type != report::type::OUTPUT)))
        {
            // invalid size or register
            return false;
        }

        return set_report(type, command_data.subspan(cmd_size + sizeof(data_reg)));
    }

    default:
        break;
    }

#if C2USB_I2C_HID_FULL_COMMAND_SUPPORT
    const short_data& u16_data =
        *reinterpret_cast<const short_data*>(command_data.data() + cmd_size + sizeof(data_reg));

    if ((command_data.size() != (cmd_size + sizeof(data_reg) + sizeof(short_data))) or
        (data_reg != registers::DATA) or not u16_data.valid_size())
    {
        // invalid size or register
        return false;
    }

    uint16_t value = u16_data.value;
    switch (cmd.opcode())
    {
    case opcode::SET_IDLE:
        return app_.set_idle(value, cmd.report_id());

    case opcode::SET_PROTOCOL:
        // SPEC WTF: why isn't the 8-bit protocol value in the command_data.data instead of in the
        // data register?

        if (not magic_enum::enum_contains<protocol>(value))
        {
            // invalid size or register
            return false;
        }
        if (app_.get_protocol() != static_cast<protocol>(value))
        {
            app_.setup(this, static_cast<protocol>(value));
        }
        return true;

    default:
        break;
    }
#endif
    return false;
}

void device::process_write(size_t data_length)
{
    std::span<uint8_t> data{buffer_.data(), data_length};
    uint16_t reg = *reinterpret_cast<const le_uint16_t*>(data.data());

    if (reg == registers::OUTPUT_REPORT)
    {
        // output report received
        set_report(report::type::OUTPUT, data.subspan(sizeof(reg)));
    }
    else if (reg == registers::COMMAND)
    {
        // set command received
        set_command(data.subspan(sizeof(reg)));
    }
    else
    {
        // invalid register
    }
}

void device::process_input_complete(size_t data_length)
{
    std::span<const uint8_t> input_data;

    // verify sent length
    if (in_queue_.front(input_data) and ((REPORT_LENGTH_SIZE + input_data.size()) <= data_length))
    {
        // input report transmit complete, remove from queue
        in_queue_.pop();

        // if the sent data is a reset response (instead of length, the first 2 bytes are 0)
        if (input_data.size() == 0)
        {
#if not C2USB_I2C_HID_FULL_COMMAND_SUPPORT
            // completed reset, initialize application
            app_.setup(this, protocol::REPORT);
#endif
        }
        else
        {
            app_.in_report_sent(input_data);
        }
    }
    else if (!in_queue_.empty())
    {
        // assert interrupt line if input reports are pending
        slave().set_pin_interrupt(true);
    }
}

void device::process_reply_complete(size_t data_length)
{
#if C2USB_I2C_HID_FULL_COMMAND_SUPPORT
    uint16_t reg = *reinterpret_cast<const le_uint16_t*>(buffer_.data());

    if (reg == registers::REPORT_DESCRIPTOR)
    {
        // initialize applications
        app_.setup(this, protocol::REPORT);
    }
#endif
}

void device::on_stop(i2c::direction dir, size_t data_length)
{
    // received request from host
    if (dir == i2c::direction::WRITE)
    {
        process_write(data_length);
    }
    else if (stage_ == 0)
    {
        // input register transmit complete
        process_input_complete(data_length);
    }
    else
    {
        // reply transmit complete
        process_reply_complete(data_length);
    }

    // reset I2C restart counter
    stage_ = 0;
}
