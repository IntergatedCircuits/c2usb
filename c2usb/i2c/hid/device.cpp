// SPDX-License-Identifier: MPL-2.0
#include "i2c/hid/device.hpp"
#include <magic_enum.hpp>

using namespace ::hid;

namespace i2c::hid
{
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

    transport::stop(app_, session_);

    // clear context
    in_queue_.clear();
    rx_buffers_ = {};
}

void device::link_reset()
{
    // reset application
    transport::stop(app_, session_);

    // clear context
    in_queue_.clear();
    rx_buffers_ = {};

    // send 0 length input report
    queue_input_report(std::span<const uint8_t>());
}

c2usb::result device::receive_report([[maybe_unused]] session& sess, const std::span<uint8_t>& data,
                                     report::type type)
{
    assert(type != report::type::INPUT);
    if ((rx_buffers_[type].size() == 0) or (stage_ == 0))
    {
        // save the target buffer for when the transfer is made
        rx_buffers_[type] = data;
        return c2usb::result::ok;
    }
    else
    {
        // the previously passed buffer is being used for receiving data
        return c2usb::result::device_or_resource_busy;
    }
}

c2usb::result device::send_report([[maybe_unused]] session& sess,
                                  const std::span<const uint8_t>& data)
{
    return queue_input_report(data) ? c2usb::result::ok : c2usb::result::device_or_resource_busy;
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
    if (session_ == nullptr)
    {
        return false;
    }
    auto data = session_->get_report(select, std::span(buffer_).subspan(sizeof(le_uint16_t)));
    if (!data.empty())
    {
        auto& report_length = *get_buffer<le_uint16_t>();
        report_length = static_cast<uint16_t>(data.size());

        // send the length 2 bytes, followed by the report data
        slave().send(&report_length, data);
        return true;
    }
    return false;
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
        if (not magic_enum::enum_contains<report::type>(type))
        {
            return false;
        }
        return get_report(cmd.report_selector());
    }

#if CONFIG_C2USB_I2C_HID_FULL_COMMAND_SUPPORT
    case opcode::GET_IDLE:
        if (session_ != nullptr)
        {
            send_short_data(static_cast<uint16_t>(session_->get_idle(cmd.report_id())));
            return true;
        }
        return false;

    case opcode::GET_PROTOCOL:
        if (session_ != nullptr)
        {
            send_short_data(static_cast<uint16_t>(session_->protocol()));
            return true;
        }
        return false;
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
        size_t header_size = static_cast<size_t>(data.data() - buffer_.data()) + sizeof(length);
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
        if (session_ != nullptr)
        {
            session_->set_report(type, report);

            return true;
        }
    }
    return false;
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

#if CONFIG_C2USB_I2C_HID_FULL_COMMAND_SUPPORT
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
        if (session_ != nullptr)
        {
            return session_->set_idle(cmd.report_id(), value);
        }
        return false;

#if CONFIG_C2USB_I2C_HID_BOOT_MODE
    case opcode::SET_PROTOCOL:
        // SPEC WTF: why isn't the 8-bit protocol value in the command_data.data instead of in the
        // data register?

        if (not magic_enum::enum_contains<protocol>(value))
        {
            return false;
        }
        transport::start(app_, session_, {this, channel::I2C, CONFIG_C2USB_I2C_HID_BOOT_MODE});
        return true;
#endif // CONFIG_C2USB_I2C_HID_BOOT_MODE

    default:
        break;
    }
#endif // CONFIG_C2USB_I2C_HID_FULL_COMMAND_SUPPORT
    return false;
}

void device::process_write(size_t data_length)
{
    std::span<uint8_t> data{buffer_.data(), data_length};
    uint16_t reg = *reinterpret_cast<const le_uint16_t*>(data.data());
    data = data.subspan(sizeof(reg));

    if (reg == registers::OUTPUT_REPORT)
    {
        // output report received
        set_report(report::type::OUTPUT, data);
    }
    else if (reg == registers::COMMAND)
    {
        // set command received
        set_command(data);
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
#if !CONFIG_C2USB_I2C_HID_FULL_COMMAND_SUPPORT
            // completed reset, initialize application
            transport::start(app_, session_, {this, channel::I2C, boot::mode::NONE});
#endif
        }
        else if (session_ != nullptr)
        {
            session_->report_sent(input_data);
        }
    }
    else if (!in_queue_.empty())
    {
        // assert interrupt line if input reports are pending
        slave().set_pin_interrupt(true);
    }
}

void device::process_reply_complete([[maybe_unused]] size_t data_length)
{
#if CONFIG_C2USB_I2C_HID_FULL_COMMAND_SUPPORT
    uint16_t reg = *reinterpret_cast<const le_uint16_t*>(buffer_.data());

    if (reg == registers::REPORT_DESCRIPTOR)
    {
        // initialize applications
        transport::start(app_, session_, {this, channel::I2C, boot::mode::NONE});
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

} // namespace i2c::hid
