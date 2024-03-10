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
#ifndef __HID_APPLICATION_HPP_
#define __HID_APPLICATION_HPP_

#include <atomic>
#include <hid/report.hpp>

#include "usb/base.hpp"

namespace hid
{
using namespace c2usb;

struct report_protocol;

/// @brief  Interface for different transport layers.
class transport : public interface
{
  public:
    virtual result send_report(const std::span<const uint8_t>& data,
                               report::type type = report::type::INPUT) = 0;
    virtual result receive_report(const std::span<uint8_t>& data,
                                  report::type type = report::type::OUTPUT) = 0;
};

/// @brief  The application is the base class for transport-agnostic HID device-side applications.
class application : public polymorphic
{
  public:
    constexpr explicit application(const report_protocol& rp)
        : report_info_(&rp)
    {}

    constexpr const report_protocol& report_info() const { return *report_info_; }

  private: // internal behavior to override
    /// @brief Initialize the application when the protocol has been (possibly implicitly) selected.
    /// @param prot: the protocol to start operating with
    /// @return true if successful, false otherwise
    virtual void start(protocol prot) {}

    /// @brief Stop and clean up the application when the transport is shut down.
    virtual void stop() {}

  public:
    /// @brief Called by the transport when a report is received from the host.
    ///        The report data is always loaded into the buffer provided by @ref receive_report
    ///        call.
    /// @param type: received report's type (either OUTPUT or FEATURE)
    /// @param data: received report data
    virtual void set_report(report::type type, const std::span<const uint8_t>& data) = 0;

    /// @brief Called by the transport when a synchronous report reading is requested
    ///        by the host. Use @ref send_report to provide the requested report data.
    /// @param select: identifies the requested report (either INPUT or FEATURE)
    /// @param buffer: optional buffer space that is available for the report sending
    virtual void get_report(report::selector select, const std::span<uint8_t>& buffer) = 0;

    /// @brief Called by the transport when the host has received the INPUT report
    ///        that was provided through a successful call of @ref send_report,
    ///        outside of @ref get_report context.
    /// @param data: the report data that was sent
    virtual void in_report_sent(const std::span<const uint8_t>& data) {}

    /// @brief  Indicates the currently selected protocol.
    /// @return The current protocol, either REPORT (default) or BOOT.
    virtual protocol get_protocol() const { return protocol::REPORT; }

  protected: // internal API to the transport
    /// @brief  Send a report to the host.
    /// @param  data: the report data to send
    /// @param  type: either INPUT, or FEATURE (the latter only from @ref get_report call context)
    /// @return OK           if transmission is scheduled,
    ///         BUSY         if transport is busy with another report,
    ///         NO_TRANSPORT if transport is missing;
    ///         INVALID      if the buffer size is 0 or FEATURE report is provided from wrong
    ///                      context
    result send_report(const std::span<const uint8_t>& data,
                       report::type type = report::type::INPUT)
    {
        assert(data.size() > 0);
        auto tp = transport_.load();
        if (tp)
        {
            return tp->send_report(data, type);
        }
        else
        {
            return result::NO_TRANSPORT;
        }
    }

    /// @brief  Send a @ref hid::report::data type structure to the host.
    /// @tparam T: a @ref hid::report::data type
    /// @param  report: the report data to send
    /// @return see @ref send_report above
    template <typename T>
    result send_report(const T* report)
        requires(::hid::report::Data<T>)
    {
        return send_report(std::span<const uint8_t>(report->data(), sizeof(*report)),
                           report->type());
    }

    /// @brief  Request receiving the next OUT or FEATURE report into the provided buffer.
    /// @param  data: The allocated buffer for receiving reports.
    /// @param  type: either OUTPUT, or FEATURE
    /// @return OK           if transport is available
    ///         NO_TRANSPORT if transport is missing
    ///         INVALID      if the buffer size is 0
    result receive_report(const std::span<uint8_t>& data, report::type type = report::type::OUTPUT)
    {
        assert(data.size() > 0);
        auto tp = transport_.load();
        if (tp)
        {
            return tp->receive_report(data, type);
        }
        else
        {
            return result::NO_TRANSPORT;
        }
    }

    /// @brief  Request receiving the next OUT or FEATURE report into the provided
    ///         @ref hid::report::data type buffer.
    /// @tparam T: a @ref hid::report::data type
    /// @param  report: the report buffer to receive into
    /// @return see @ref receive_report above
    template <typename T>
    result receive_report(T* report)
        requires(::hid::report::Data<T>)
    {
        return receive_report(std::span<uint8_t>(report->data(), sizeof(*report)), report->type());
    }

    /// @brief  Changes the report information on the fly. This is extremely rarely needed.
    /// @param  rp: the new report protocol to use
    void set_report_info(const report_protocol& rp)
    {
        // assert(!has_transport());
        report_info_ = &rp;
    }

  public:
    // SPEC WTF: The IDLE parameter is meant to control how often the device should resend
    // the same piece of information. This is introduced in the USB HID class spec,
    // somehow thinking that the host cannot keep track of the time by itself -
    // but then how does the host generate the Start of Frame with correct intervals?
    uint32_t get_idle(uint8_t report_id = 0) { return 0; }
    bool set_idle(uint32_t idle_repeat_ms, uint8_t report_id = 0) { return idle_repeat_ms == 0; }

    bool has_transport() const { return transport_.load() != nullptr; }

    bool has_transport(transport* tp) const { return transport_.load() == tp; }

    bool setup(transport* tp, protocol prot)
    {
        transport* expected = nullptr;
        if (transport_.compare_exchange_strong(expected, tp))
        {
            start(prot);
            return true;
        }
        if (expected != tp)
        {
            return false;
        }
        if (get_protocol() != prot)
        {
            stop();
            start(prot);
        }
        return true;
    }

    bool teardown(transport* tp)
    {
        if (transport_.compare_exchange_strong(tp, nullptr))
        {
            stop();
            return true;
        }
        else
        {
            return false;
        }
    }

  private:
    const report_protocol* report_info_;
    std::atomic<transport*> transport_{};
};

/// @brief  Helper class to manage FEATURE and OUTPUT buffers.
class reports_receiver
{
  public:
    constexpr reports_receiver() = default;

    constexpr std::span<uint8_t>& operator[](report::type type)
    {
        return buffers_[static_cast<uint8_t>(type) - OFFSET];
    }
    constexpr const std::span<uint8_t>& operator[](report::type type) const
    {
        return buffers_[static_cast<uint8_t>(type) - OFFSET];
    }
    constexpr std::span<uint8_t>& largest()
    {
        return (buffers_[0].size() > buffers_[1].size()) ? buffers_[0] : buffers_[1];
    }

  private:
    constexpr static auto OFFSET =
        static_cast<uint8_t>(std::min(report::type::FEATURE, report::type::OUTPUT));
    static_assert((static_cast<uint8_t>(std::max(report::type::FEATURE, report::type::OUTPUT)) -
                   OFFSET) == 1);

    std::array<std::span<uint8_t>, 2> buffers_{};
};
} // namespace hid

#endif // __HID_APPLICATION_HPP_
