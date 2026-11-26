// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <tuple>
#include "hid/transport.hpp"
#include "i2c/hid/standard.hpp"
#include "i2c/slave.hpp"

inline namespace utilities
{
template <typename T>
consteval T pack_str(const char* str)
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
class device : public i2c::slave::module, public ::hid::transport
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
    /// @brief  The largest overhead of a host write before the report data:
    ///         register (2) + extended command (3) + data register (2) + report length (2)
    constexpr static size_t MAX_WRITE_OVERHEAD =
        sizeof(uint16_t) + 3 + sizeof(uint16_t) + REPORT_LENGTH_SIZE;

    /// @brief  Calculates the size of the transfer buffer needed for an application.
    ///         The buffer receives the complete host writes (including OUTPUT and FEATURE
    ///         reports), and holds the device's replies (e.g. HID descriptor).
    /// @param  rp: the application's report protocol
    /// @return the minimal buffer size in bytes
    constexpr static size_t buffer_size(const ::hid::report_protocol& rp)
    {
        return std::max(sizeof(descriptor),
                        MAX_WRITE_OVERHEAD +
                            std::max<size_t>(rp.max_output_size, rp.max_feature_size));
    }

    /// @brief  Creates the I2C HID device, and starts listening on the I2C bus.
    /// @param  app: the HID application
    /// @param  pinfo: product information, reported in the HID descriptor
    /// @param  slave: the I2C slave driver
    /// @param  address: the I2C address of the device
    /// @param  hid_descriptor_reg_address: the register address of the HID descriptor
    /// @param  buffer: the transfer buffer, at least @ref buffer_size() large for the application
    device(::hid::application& app, const hid::product_info& pinfo, i2c::slave& slave,
           i2c::address address, uint16_t hid_descriptor_reg_address,
           const std::span<uint8_t>& buffer);
    ~device() override;

    device(const device&) = delete;
    device& operator=(const device&) = delete;
    device(device&&) = delete;
    device& operator=(device&&) = delete;

    [[nodiscard]] bool get_power_state() const { return powered_; }

  private:
    void link_reset();

    [[nodiscard]] uint16_t hid_descriptor_reg_address() const { return hid_descriptor_reg_; }
    void get_hid_descriptor(descriptor& desc) const;
    c2usb::result send_report(::hid::session& sess, const std::span<const uint8_t>& data) override;
    c2usb::result receive_report(::hid::session& sess, const std::span<uint8_t>& data,
                                 ::hid::report::type type) override;
    c2usb::result cancel_report_send(::hid::session& sess) override;

    bool on_start(i2c::direction dir, size_t data_length) override;
    void on_stop(i2c::direction dir, size_t data_length) override;

    template <typename T>
    [[nodiscard]] T* get_buffer()
    {
        // NOLINTNEXTLINE(*-type-reinterpret-cast)
        return reinterpret_cast<T*>(buffer_.data());
    }

    void queue_reset_response();

    void send_short_data(uint16_t value);
    bool get_report(::hid::report::selector select);
    bool get_command(const std::span<const uint8_t>& command_data);

    bool reply_request(size_t data_length);

    /// @brief  Owner of the queued input report, only the owner may consume it.
    enum class in_owner : uint8_t
    {
        NONE,
        SENDING,    // the host is reading it (I2C ISR context)
        QUEUEING,   // it's being queued (application context)
        CANCELLING, // it's being withdrawn (application context)
    };

    bool get_input();
    bool claim_input(in_owner owner)
    {
        auto expected = in_owner::NONE;
        return in_owner_.compare_exchange_strong(expected, owner);
    }
    void release_input(in_owner owner)
    {
        // only the current owner can release it
        std::ignore = in_owner_.compare_exchange_strong(owner, in_owner::NONE);
    }

    void set_power(bool powered);
    bool set_report(::hid::report::type type, const std::span<const uint8_t>& data);
    bool set_command(const std::span<const uint8_t>& command_data);
    void process_write(size_t data_length);
    void process_input_complete(size_t data_length);
    void process_reply_complete(size_t data_length);

    i2c::slave& slave() { return slave_; }

    ::hid::application& app_;
    ::hid::session* session_{};
    const hid::product_info& pinfo_;
    reports_receiver rx_buffers_;
    i2c::slave& slave_;
    uint16_t hid_descriptor_reg_;
    uint8_t stage_{};
    bool powered_{};
    std::optional<std::span<const uint8_t>> in_queue_;
    std::atomic<in_owner> in_owner_{};
    std::span<uint8_t> buffer_;
};

} // namespace i2c::hid
