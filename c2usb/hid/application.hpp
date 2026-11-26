// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <atomic>
#include "usb/base.hpp"
#include <hid/report.hpp>
#include <hid/report_protocol.hpp>

// vendor headers (e.g. STM32 CMSIS) may define these as peripheral instance macros
#pragma push_macro("I2C")
#pragma push_macro("SPI")
#pragma push_macro("USB")
#undef I2C
#undef SPI
#undef USB
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
#pragma pop_macro("USB")
#pragma pop_macro("SPI")
#pragma pop_macro("I2C")

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

    [[nodiscard]] hid::protocol protocol() const
    {
        return (boot_mode_ == hid::boot::mode::NONE) ? hid::protocol::REPORT : hid::protocol::BOOT;
    }
    [[nodiscard]] hid::boot::mode boot_protocol() const { return boot_mode_; }
    [[nodiscard]] hid::channel channel() const { return channel_; }

    /// @brief  Whether the host has suspended this session's link.
    [[nodiscard]] bool suspended() const { return suspended_; }

    /// @brief  Notification that the host has suspended or resumed this session's link.
    ///         Called only on state change, @ref suspended() already reflects the new state.
    virtual void suspend_callback([[maybe_unused]] bool suspended) {}

    /// @brief  Send an INPUT report to the host. This is the only device-initiated data transfer.
    ///         The report data must remain valid until @ref send_report_callback is invoked.
    /// @return ok: the report is queued for sending
    ///         device_or_resource_busy: a previous report is still pending, wait for its
    ///         @ref send_report_callback, or withdraw it with @ref cancel_report_send
    ///         connection_reset: the session is no longer active
    c2usb::result send_report(const std::span<const uint8_t>& data);

    template <report::Data T>
    c2usb::result send_report(const T* report)
        requires(T::type() == report::type::INPUT)
    {
        return send_report(std::span<const uint8_t>(report->data(), sizeof(*report)));
    }

    /// @brief  Feedback that the INPUT report send operation has completed.
    ///         The result is operation_canceled if the report was withdrawn by
    ///         @ref cancel_report_send.
    virtual void send_report_callback([[maybe_unused]] report::selector select,
                                      [[maybe_unused]] c2usb::result result)
    {}

    /// @brief  Request to withdraw the INPUT report that was passed to @ref send_report,
    ///         but hasn't been delivered to the host yet.
    ///         If a report is pending, its outcome is reported through @ref send_report_callback,
    ///         as the transfer may still complete before the cancellation takes effect.
    ///         The callback may be invoked before this function returns.
    /// @return ok: the cancellation is issued, or there is no pending report
    ///         operation_in_progress: the report transfer may be under way, it cannot be cancelled
    ///         operation_not_supported: the transport doesn't support cancellation
    c2usb::result cancel_report_send();

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

    ~session() override
    {
        tp_.store(nullptr);
        channel_ = channel::NONE;
    }
    session(const session&) = delete;
    session& operator=(const session&) = delete;
    session(session&&) = delete;
    session& operator=(session&&) = delete;

    [[nodiscard]] uint32_t get_idle(uint8_t report_id = 0,
                                    [[maybe_unused]] unsigned multiplier = 1) const
    {
        return report_id == 0 ? idle_rate_ : 0;
    }

    bool set_idle(uint8_t report_id, uint8_t idle_repeat, [[maybe_unused]] unsigned multiplier)
    {
        if (report_id == 0)
        {
            idle_rate_ = idle_repeat;
        }
        // we reject even the idea of a non-zero idle rate,
        // but keep the value as it's a good macOS detection mechanism
        return idle_repeat == 0;
    }
    bool set_idle([[maybe_unused]] uint8_t report_id, [[maybe_unused]] uint16_t idle_repeat)
    {
        // in case I2C HID implements it
        return idle_repeat == 0;
    }

  private:
    friend class transport;

    std::atomic<transport*> tp_;
    hid::channel channel_;
    hid::boot::mode boot_mode_;
    bool suspended_{};

    // these fields are managed by the application/session
    uint8_t idle_rate_{};
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
    [[nodiscard]] virtual session& start(const session::params& params) = 0;

    /// @brief  Stop the given session. The application is expected to clean up any resources
    ///         associated with the session and delete it if necessary.
    virtual void stop(session& sess) = 0;

    [[nodiscard]] constexpr const report_protocol& report_info() const { return report_info_; }

  protected:
    // allow subclasses to modify the report descriptor at runtime (for advanced use cases)
    // NOLINTNEXTLINE(*-non-private-member-variables-in-classes)
    report_protocol report_info_;
};

} // namespace hid
