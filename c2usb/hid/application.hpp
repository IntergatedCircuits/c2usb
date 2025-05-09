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
#include <hid/report_protocol.hpp>

#include "reference_array_view.hpp"
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
        : report_info_(rp)
    {}

    constexpr const report_protocol& report_info() const { return report_info_; }

  private: // internal behavior to override
    /// @brief Initialize the application when the protocol has been (possibly implicitly) selected.
    /// @param prot: the protocol to start operating with
    virtual void start([[maybe_unused]] protocol prot) {}

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
    virtual void in_report_sent([[maybe_unused]] const std::span<const uint8_t>& data) {}

    /// @brief  Indicates the currently selected protocol.
    /// @return The current protocol, either REPORT (default) or BOOT.
    virtual protocol get_protocol() const { return protocol::REPORT; }

  protected: // internal API to the transport
    /// @brief  Send a report to the host.
    /// @param  data: the report data to send
    /// @param  type: either INPUT, or FEATURE (the latter only from @ref get_report call context)
    /// @return @ref result::ok if transmission is scheduled,
    ///         @ref result::device_or_resource_busy if transport is busy with another report,
    ///         @ref result::connection_reset if transport is missing;
    ///         @ref result::invalid_argument if the buffer size is 0 or FEATURE report is provided
    ///         from wrong context
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
            return result::connection_reset;
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
    /// @return @ref result::ok if transport is available
    ///         @ref result::connection_reset if transport is missing
    ///         @ref result::invalid_argument if the buffer size is 0
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
            return result::connection_reset;
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

  public:
    // SPEC WTF: The IDLE parameter is meant to control how often the device should resend
    // the same piece of information. This is introduced in the USB HID class spec,
    // somehow thinking that the host cannot keep track of the time by itself -
    // but then how does the host generate the Start of Frame with correct intervals?
    uint32_t get_idle([[maybe_unused]] uint8_t report_id = 0) { return 0; }
    bool set_idle(uint32_t idle_repeat_ms, [[maybe_unused]] uint8_t report_id = 0)
    {
        return idle_repeat_ms == 0;
    }

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

  protected:
    const transport* get_transport() const { return transport_.load(); }
    report_protocol report_info_;

  private:
    std::atomic<transport*> transport_{};
    friend class multi_application;
};

/// @brief  The multi_application is a helper to combine multiple TLC applications into a single HID
///         instance. If boot protocol is supported, its application must be the first parameter.
class multi_application : public application
{
  protected:
    multi_application(const report_protocol& prot, reference_array_view<application> apps)
        : application(prot), apps_(apps)
    {}

  private:
    template <typename T = void, typename... Targs1, typename... Targs2>
    void dispatch(T (application::*method)(Targs1...), Targs2&&... args) const
    {
        if (get_protocol() == protocol::BOOT)
        {
            (apps_[0]->*method)(std::forward<Targs1>(args)...);
        }
        else
        {
            for (auto* app : apps_)
            {
                (app->*method)(std::forward<Targs1>(args)...);
            }
        }
    }
    void start(protocol prot) override
    {
        transport_copy_ = transport_.load();
        if (prot == protocol::BOOT)
        {
            apps_[0]->setup(transport_copy_, prot);
        }
        else
        {
            for (auto* app : apps_)
            {
                app->setup(transport_copy_, prot);
            }
        }
    }
    void stop() override { dispatch(&application::teardown, transport_copy_); }
    void set_report(report::type type, const std::span<const uint8_t>& data) override
    {
        dispatch(&application::set_report, type, data);
    }
    void get_report(report::selector select, const std::span<uint8_t>& buffer) override
    {
        dispatch(&application::get_report, select, buffer);
    }
    void in_report_sent(const std::span<const uint8_t>& data) override
    {
        dispatch(&application::in_report_sent, data);
    }
    protocol get_protocol() const override { return apps_[0]->get_protocol(); }

    const reference_array_view<application> apps_;
    transport* transport_copy_{};
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
