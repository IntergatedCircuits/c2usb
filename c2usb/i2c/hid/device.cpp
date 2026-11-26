// SPDX-License-Identifier: MPL-2.0
#include "i2c/hid/device.hpp"
#include <magic_enum.hpp>

using namespace ::hid;

static uint16_t get_u16(const uint8_t* data)
{
    return *c2usb::std_layout_cast<const c2usb::le_uint16_t*>(data);
}

namespace i2c::hid
{
device::device(application& app, const product_info& pinfo, i2c::slave& slave, i2c::address address,
               uint16_t hid_descriptor_reg_address, const std::span<uint8_t>& buffer)
    : module(address),
      app_(app),
      pinfo_(pinfo),
      slave_(slave),
      hid_descriptor_reg_(hid_descriptor_reg_address),
      buffer_(buffer)
{
    assert(buffer_.size() >= buffer_size(app_.report_info()));
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
    in_queue_.reset();
    in_owner_.store(in_owner::NONE);
    rx_buffers_ = {};
}

void device::link_reset()
{
    // reset application
    transport::stop(app_, session_);

    // clear context
    in_queue_.reset();
    in_owner_.store(in_owner::NONE);
    rx_buffers_ = {};
    // reset puts the device in ON power state
    powered_ = true;

    // send 0 length input report
    queue_reset_response();
}

c2usb::result device::receive_report([[maybe_unused]] session& sess, const std::span<uint8_t>& data,
                                     report::type type)
{
    assert(type != report::type::INPUT);
    // the host writes are received into the device's own buffer, the report is only copied
    // into the application's buffer when the transfer completes, so it can be replaced anytime
    rx_buffers_[type] = data;
    return c2usb::result::ok;
}

c2usb::result device::send_report([[maybe_unused]] session& sess,
                                  const std::span<const uint8_t>& data)
{
    // while claimed, the queue cannot be consumed by the I2C ISR
    if (not claim_input(in_owner::QUEUEING))
    {
        // the host is reading the previous report
        return c2usb::result::device_or_resource_busy;
    }
    if (in_queue_.has_value())
    {
        // a single report can be pending, it must be delivered or cancelled first
        release_input(in_owner::QUEUEING);
        return c2usb::result::device_or_resource_busy;
    }
    in_queue_.emplace(data);
    release_input(in_owner::QUEUEING);

    // assert the interrupt line only after releasing the queue: if the host reads the input
    // register while it's claimed, it gets an empty report, and the line is deasserted
    slave().set_pin_interrupt(true);
    return c2usb::result::ok;
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
    auto data = session_->get_report(select, buffer_.subspan(sizeof(le_uint16_t)));
    if (!data.empty())
    {
        // the length field includes its own size
        auto& report_length = *get_buffer<le_uint16_t>();
        report_length = static_cast<uint16_t>(REPORT_LENGTH_SIZE + data.size());

        // send the length 2 bytes, followed by the report data
        slave().send(&report_length, data);
        return true;
    }
    return false;
}

bool device::get_command(const std::span<const uint8_t>& command_data)
{
    const auto& cmd = *c2usb::std_layout_cast<const command_view*>(command_data.data());
    auto cmd_size = cmd.size();
    uint16_t data_reg = get_u16(command_data.data() + cmd_size);

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
    uint16_t reg = get_u16(data.data());

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
        if (reg == registers::REPORT_DESCRIPTOR)
        {
            // Report descriptor lets the host interpret the raw report data
            const auto& rdesc = app_.report_info().descriptor;

            slave().send(rdesc.to_span());
            return true;
        }
        // else
        {
            // invalid size or register
            return false;
        }
    }
    if ((data.size() > sizeof(reg)) and (reg == registers::COMMAND))
    {
        return get_command(data.subspan(sizeof(reg)));
    }
    // else
    {
        // invalid size or register
        return false;
    }
}

void device::queue_reset_response()
{
    // called from the I2C ISR, replacing any pending report (the session is already stopped)
    in_queue_.emplace();
    slave().set_pin_interrupt(true);
}

c2usb::result device::cancel_report_send(session& sess)
{
    if (not claim_input(in_owner::CANCELLING))
    {
        // the host is reading the queued report
        return c2usb::result::operation_in_progress;
    }
    // while claimed, the queued report cannot be consumed by the I2C ISR
    if (not in_queue_.has_value() or in_queue_->empty())
    {
        // nothing to cancel (the reset response is not cancellable)
        release_input(in_owner::CANCELLING);
        return c2usb::result::ok;
    }
    auto select = report::selector(report::type::INPUT,
                                   app_.report_info().report_id_present ? in_queue_->front() : 0);
    in_queue_.reset();
    // if the host still reads in response to an already observed interrupt,
    // it gets an empty report, which it ignores
    slave().set_pin_interrupt(false);
    release_input(in_owner::CANCELLING);

    sess.send_report_callback(select, c2usb::result::operation_canceled);
    return c2usb::result::ok;
}

bool device::get_input()
{
    // send the next report from the queue, unless it's being cancelled
    bool sending = claim_input(in_owner::SENDING);
    if (sending and in_queue_.has_value() and not in_queue_->empty())
    {
        auto input_data = in_queue_.value();

        // the length field includes its own size
        auto& report_length = *c2usb::std_layout_cast<le_uint16_t*>(buffer_.data());
        report_length = static_cast<uint16_t>(REPORT_LENGTH_SIZE + input_data.size());

        slave().send(&report_length, input_data);
    }
    else
    {
        release_input(in_owner::SENDING);
        // this is a reset, or the master is only checking our presence on the bus
        // NOLINTNEXTLINE(*-type-reinterpret-cast)
        auto& reset_sentinel = *c2usb::std_layout_cast<le_uint16_t*>(buffer_.data());
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
            slave().receive(buffer_);
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

void device::set_power(bool powered)
{
    if (powered_ != powered)
    {
        powered_ = powered;
        transport::suspend(session_, not powered);
    }
}

bool device::set_report(report::type type, const std::span<const uint8_t>& data)
{
    // the complete write is in buffer_, data starts with the report length field
    if ((data.size() < REPORT_LENGTH_SIZE) or (get_u16(data.data()) != data.size()))
    {
        // invalid length
        return false;
    }
    auto report_data = data.subspan(REPORT_LENGTH_SIZE);
    auto& target_buffer = rx_buffers_[type];
    if ((session_ == nullptr) or (report_data.size() > target_buffer.size()))
    {
        // no session, or no (sufficient) buffer was provided by the application for this type
        return false;
    }

    // copy the report into the buffer provided for this report type
    std::copy(report_data.begin(), report_data.end(), target_buffer.begin());
    auto report = target_buffer.first(report_data.size());

    // clear the buffer before the callback, so it can set a new buffer
    target_buffer = {};

    // pass it to the app
    session_->set_report(type, report);
    return true;
}

bool device::set_command(const std::span<const uint8_t>& command_data)
{
    const auto& cmd = *c2usb::std_layout_cast<const command_view*>(command_data.data());
    auto cmd_size = cmd.size();
    uint16_t data_reg = get_u16(command_data.data() + cmd_size);

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
    const short_data& u16_data = *c2usb::std_layout_cast<const short_data*>(
        command_data.data() + cmd_size + sizeof(data_reg));

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
        transport::start(app_, session_,
                         {.transport = this,
                          .channel = channel::I2C,
                          .boot_protocol = CONFIG_C2USB_I2C_HID_BOOT_MODE});
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
    uint16_t reg = get_u16(data.data());
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
    // release the queued report, if it was claimed by get_input()
    release_input(in_owner::SENDING);

    // verify sent length
    if (in_queue_.has_value() and ((REPORT_LENGTH_SIZE + in_queue_.value().size()) <= data_length))
    {
        auto input_data = in_queue_.value();
        // input report transmit complete, remove from queue
        in_queue_.reset();

        // if the sent data is a reset response (instead of length, the first 2 bytes are 0)
        if (input_data.empty())
        {
#if !CONFIG_C2USB_I2C_HID_FULL_COMMAND_SUPPORT
            // completed reset, initialize application
            transport::start(
                app_, session_,
                {.transport = this, .channel = channel::I2C, .boot_protocol = boot::mode::NONE});
#endif
        }
        else if (session_ != nullptr)
        {
            auto selector = report::selector(
                report::type::INPUT, app_.report_info().report_id_present ? input_data.front() : 0);
            session_->send_report_callback(selector, c2usb::result::ok);
        }
    }
    else if (in_queue_.has_value())
    {
        // assert interrupt line if input reports are pending
        slave().set_pin_interrupt(true);
    }
}

void device::process_reply_complete([[maybe_unused]] size_t data_length)
{
#if CONFIG_C2USB_I2C_HID_FULL_COMMAND_SUPPORT
    uint16_t reg = get_u16(buffer_.data());

    if (reg == registers::REPORT_DESCRIPTOR)
    {
        // initialize applications
        transport::start(
            app_, session_,
            {.transport = this, .channel = channel::I2C, .boot_protocol = boot::mode::NONE});
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
