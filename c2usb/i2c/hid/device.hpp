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
#ifndef __I2C_HID_DEVICE_HPP_
#define __I2C_HID_DEVICE_HPP_

#include <etl/delegate.h>

#include "hid/application.hpp"
#include "i2c/hid/standard.hpp"
#include "i2c/slave.hpp"
#include "single_elem_queue.hpp"

inline namespace utilities
{
template <typename T>
constexpr T pack_str(const char* str)
{
    T val = 0;
    for (unsigned i = 0; (i < sizeof(T)) and (str[i] != '\0'); i++)
    {
        val = (val) | static_cast<T>(str[i]) << (i * 8);
    }
    return val;
}
} // namespace utilities

namespace i2c::hid
{
/// @brief  Implementation of the I2C HID protocol on the slave device side.
class device : public slave::module, public ::hid::transport
{
    enum registers : uint16_t
    {
        HID_DESCRIPTOR = pack_str<uint16_t>("H"), // actually configured in device constructor
        REPORT_DESCRIPTOR = pack_str<uint16_t>("RD"),
        COMMAND = pack_str<uint16_t>("CM"),
        DATA = pack_str<uint16_t>("DT"),
        INPUT_REPORT = pack_str<uint16_t>("IR"), // SPEC WTF:
                                                 // only set in descriptor, never seen on the bus
        OUTPUT_REPORT = pack_str<uint16_t>("OR"),
    };

  public:
    device(::hid::application& app, const hid::product_info& pinfo, i2c::slave& slave,
           i2c::address address, uint16_t hid_descriptor_reg_address);
    ~device() override;

    enum class event : uint8_t
    {
        POWER_STATE_CHANGE = 0,
    };
    using power_event_delegate = etl::delegate<void(device&, event)>;
    void set_power_event_delegate(const power_event_delegate& delegate)
    {
        power_event_delegate_ = delegate;
    }

    bool get_power_state() const { return powered_; }

  private:
    void link_reset();

    uint16_t hid_descriptor_reg_address() const { return hid_descriptor_reg_; }
    void get_hid_descriptor(descriptor& desc) const;
    result send_report(const std::span<const uint8_t>& data, ::hid::report::type type) override;
    result receive_report(const std::span<uint8_t>& data, ::hid::report::type type) override;

    void delegate_power_event(event ev);

    bool on_start(i2c::direction dir, size_t data_length) override;
    void on_stop(i2c::direction dir, size_t data_length) override;

    template <typename T>
    T* get_buffer()
    {
        return reinterpret_cast<T*>(buffer_.data());
    }

    bool queue_input_report(const std::span<const uint8_t>& data);

    void send_short_data(uint16_t value);
    bool get_report(::hid::report::selector select);
    bool get_command(const std::span<const uint8_t>& command_data);

    bool reply_request(size_t data_length);

    bool get_input();

    void set_power(bool powered);
    bool set_report(::hid::report::type type, const std::span<const uint8_t>& data);
    bool set_command(const std::span<const uint8_t>& command_data);
    void process_write(size_t data_length);
    void process_input_complete(size_t data_length);
    void process_reply_complete(size_t data_length);

    i2c::slave& slave() { return slave_; }

    device(const device&) = delete;
    device& operator=(const device&) = delete;

    ::hid::application& app_;
    const hid::product_info& pinfo_;
    power_event_delegate power_event_delegate_{};
    ::hid::reports_receiver rx_buffers_{};
    i2c::slave& slave_;
    uint16_t hid_descriptor_reg_;
    single_elem_queue<std::span<const uint8_t>> in_queue_{};
    uint8_t stage_{};
    bool powered_{};
    ::hid::report::selector get_report_{};
    std::array<uint8_t, sizeof(descriptor)> buffer_{};
};
} // namespace i2c::hid

#endif // __I2C_HID_DEVICE_HPP_
