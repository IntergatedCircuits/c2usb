// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <atomic>
#include "usb/base.hpp"
#include <hid/report.hpp>
#include <hid/report_protocol.hpp>

namespace hid
{
enum class channel : uint8_t
{
    NONE,
    I2C,
    SPI,
    USB,
    BT_GATT,
    APPLE_IAP2, // only makes sense when iAP2 itself is over UART
};

class transport;

/// @brief  The HID session is used to manage the state of a single HID session, and is owned by
///         the application. Each connection to a host is represented by a session, and the
///         application can manage multiple sessions if needed (e.g. for BLE).
class session : public c2usb::polymorphic
{
  public:
    struct params
    {
        hid::transport* transport{};
        hid::channel channel{};
        hid::boot::mode boot_protocol{};
    };

    constexpr session(const params& p)
        : tp_(p.transport), channel_(p.channel), boot_mode_(p.boot_protocol)
    {}

    hid::protocol protocol() const
    {
        return (boot_mode_ == hid::boot::mode::NONE) ? hid::protocol::REPORT : hid::protocol::BOOT;
    }
    hid::boot::mode boot_protocol() const { return boot_mode_; }
    hid::channel channel() const { return channel_; }

    /// @brief  Send an INPUT report to the host. This is the only device-initiated data transfer.
    c2usb::result send_report(const std::span<const uint8_t>& data);

    template <report::Data T>
    c2usb::result send_report(const T* report)
        requires(T::type() == report::type::INPUT)
    {
        return send_report(std::span<const uint8_t>(report->data(), sizeof(*report)));
    }

    /// @brief  Feedback that the INPUT report has been sent.
    virtual void report_sent([[maybe_unused]] const std::span<const uint8_t>& data) {}

    /// @brief  Provide the necessary buffer to receive an OUTPUT or FEATURE report from the host.
    c2usb::result receive_report(const std::span<uint8_t>& data,
                                 report::type type = report::type::OUTPUT);

    template <report::Data T>
    c2usb::result receive_report(T* report)
        requires(T::type() != report::type::INPUT)
    {
        return receive_report(std::span<uint8_t>(report->data(), sizeof(*report)), report->type());
    }

    /// @brief  Handle an OUTPUT or FEATURE report received from the host. To continue receiving
    ///         reports of this type, the application must call receive_report again.
    virtual void set_report(report::type type, const std::span<const uint8_t>& data) = 0;

    /// @brief  Provide a report's data. The provided buffer may be used
    ///         if it has sufficient capacity for the report.
    virtual std::span<const uint8_t> get_report(report::selector select,
                                                const std::span<uint8_t>& buffer) = 0;

    virtual ~session()
    {
        tp_.store(nullptr);
        channel_ = channel::NONE;
    }

    uint32_t get_idle([[maybe_unused]] uint8_t report_id = 0,
                      [[maybe_unused]] unsigned multiplier = 1) const
    {
        return report_id == 0 ? idle_rate_ : 0;
    }

    bool set_idle([[maybe_unused]] uint8_t report_id, [[maybe_unused]] uint16_t idle_repeat,
                  [[maybe_unused]] unsigned multiplier = 1)
    {
        if (report_id == 0)
        {
            idle_rate_ = idle_repeat;
        }
        // we reject even the idea of a non-zero idle rate,
        // but keep the value as it's a good macOS detection mechanism
        return idle_repeat == 0;
    }

  private:
    std::atomic<transport*> tp_;
    hid::channel channel_;
    hid::boot::mode boot_mode_;

    // these fields are managed by the application/session
    uint16_t idle_rate_{};
};

/// @brief  This class defines the kind of HID application being implemented by means of its report
///         descriptor. It creates and destroys sessions for each transport channel as they are
///         started and stopped.
class application : public c2usb::polymorphic
{
  public:
    constexpr explicit application(const report_protocol& rp)
        : report_info_(rp)
    {}

    /// @brief  Start a new session for the given transport type and protocol. The application is
    ///         expected to create a new session object and return a reference to it. The transport
    ///         will then manage the session and call its methods as needed.
    virtual session& start(const session::params& params) = 0;

    /// @brief  Stop the given session. The application is expected to clean up any resources
    ///         associated with the session and delete it if necessary.
    virtual void stop(session& sess) = 0;

    constexpr const report_protocol& report_info() const { return report_info_; }

  protected:
    report_protocol report_info_;
};

} // namespace hid
