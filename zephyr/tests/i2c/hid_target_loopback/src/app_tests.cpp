// SPDX-License-Identifier: Apache-2.0
/*
 * I2C HID loopback tests with a dedicated recording test application,
 * covering the session lifecycle and the application <-> transport interactions:
 * - every application and session callback is recorded as an event, in order
 * - report IDs, including ones that need the extended (3 byte) command format
 * - large OUTPUT and FEATURE reports of different sizes
 * - INPUT report flow control: busy refusal, completion feedback, cancellation
 * - receive buffer flow control, and the rejection of invalid writes
 */
#include "i2c_hid_host.hpp"

#include <atomic>
#include <optional>

#include <hid/app/opaque.hpp>
#include <hid/application.hpp>
#include <hid/rdf/descriptor.hpp>
#include <hid/report_protocol.hpp>

namespace hid::page
{
enum class test_page : uint8_t;
template <>
constexpr inline auto get_info<test_page>()
{
    return info(0xFF00, 0x0007, "Test",
                [](hid::usage_id_t id) { return id ? "Test {}" : nullptr; });
}
enum class test_page : uint8_t
{
    APPLICATION = 0x0001,
    INPUT_LARGE = 0x0002,
    INPUT_EXT = 0x0003,
    OUTPUT_LARGE = 0x0004,
    FEATURE_LARGE = 0x0005,
    FEATURE_EXT = 0x0006,
    FEATURE_UNHANDLED = 0x0007,
};
} // namespace hid::page

using namespace test;
using hid::report::type;
using test_page = hid::page::test_page;

namespace
{
// report IDs at or above i2c::hid::SHORT_REPORT_ID_LIMIT need the extended command format
using input_large = hid::app::opaque::report<16, type::INPUT, 1>;
using output_large = hid::app::opaque::report<48, type::OUTPUT, 2>;
using feature_large = hid::app::opaque::report<64, type::FEATURE, 3>;
using feature_unhandled = hid::app::opaque::report<4, type::FEATURE, 4>;
using feature_ext = hid::app::opaque::report<4, type::FEATURE, 20>;
using input_ext = hid::app::opaque::report<2, type::INPUT, 21>;
static_assert(feature_ext::ID >= i2c::hid::SHORT_REPORT_ID_LIMIT);
static_assert(input_ext::ID >= i2c::hid::SHORT_REPORT_ID_LIMIT);
// OUTPUT and FEATURE maximal sizes differ, both exceed the HID descriptor size
static_assert(sizeof(output_large) != sizeof(feature_large));
static_assert(sizeof(output_large) > sizeof(i2c::hid::descriptor));

constexpr i2c::hid::product_info product_info{0x2fe3, 0x1203, i2c::hid::version("2.1")};

/// @brief  Recorded application / session callback.
struct event
{
    enum class kind : uint8_t
    {
        START,
        STOP,
        SUSPEND,
        RESUME,
        SET_REPORT,
        GET_REPORT,
        SEND_DONE,
    };
    kind what;
    type report_type{type::INPUT}; // only meaningful for report events
    uint8_t report_id;
    uint16_t size;
    int result;
    hid::channel channel;
    hid::boot::mode boot_mode;
};
K_MSGQ_DEFINE(events, sizeof(event), 16, alignof(event));
std::atomic<bool> events_overflow{};
/// @brief  Set by checks failing in the I2C ISR context, where ztest asserts can't be used.
std::atomic<bool> isr_check_failed{};

void record(const event& ev)
{
    if (k_msgq_put(&events, &ev, K_NO_WAIT) != 0)
    {
        events_overflow.store(true);
    }
}

template <typename T>
void fill(T& report, uint8_t seed)
{
    for (size_t i = 0; i < report.payload.size(); ++i)
    {
        report.payload[i] = static_cast<uint8_t>(seed + i);
    }
}

constexpr uint8_t CANARY = 0xa5;

/// @brief  HID application recording every callback, with test controlled behavior.
class recording_app : public hid::application
{
  public:
    static constexpr auto report_descriptor()
    {
        using namespace hid::rdf;
        // clang-format off
        return descriptor(
            usage_extended(test_page::APPLICATION),
            collection::application(
                hid::app::opaque::report_descriptor<input_large>(test_page::INPUT_LARGE),
                hid::app::opaque::report_descriptor<input_ext>(test_page::INPUT_EXT),
                hid::app::opaque::report_descriptor<output_large>(test_page::OUTPUT_LARGE),
                hid::app::opaque::report_descriptor<feature_large>(test_page::FEATURE_LARGE),
                hid::app::opaque::report_descriptor<feature_unhandled>(test_page::FEATURE_UNHANDLED),
                hid::app::opaque::report_descriptor<feature_ext>(test_page::FEATURE_EXT)
            )
        );
        // clang-format on
    }

  public:
    static constexpr hid::report_protocol report_protocol()
    {
        return hid::report_protocol::from_descriptor<report_descriptor()>();
    }

    static recording_app& instance()
    {
        static recording_app app;
        return app;
    }

    class session : public hid::session
    {
      public:
        session(const hid::session::params& params)
            : hid::session(params)
        {
            arm(type::OUTPUT);
            arm(type::FEATURE);
        }

        /// @brief  Provides the receive buffer of the report type, filled with a canary,
        ///         so any write into it other than the received report can be detected.
        void arm(type t)
        {
            auto buffer = rx_buffer(t);
            std::ranges::fill(buffer, CANARY);
            if (receive_report(buffer, t) != c2usb::result::ok)
            {
                isr_check_failed.store(true);
            }
        }

        std::span<uint8_t> rx_buffer(type t)
        {
            return (t == type::OUTPUT) ? std::span<uint8_t>(output_rx_)
                                       : std::span<uint8_t>(feature_rx_);
        }

        void set_report(type t, const std::span<const uint8_t>& data) override
        {
            auto& app = instance();
            std::copy(data.begin(), data.end(), app.received.begin());
            record({.what = event::kind::SET_REPORT,
                    .report_type = t,
                    .report_id = data.front(),
                    .size = static_cast<uint16_t>(data.size())});
            if (app.rearm.load())
            {
                arm(t);
            }
        }

        std::span<const uint8_t> get_report(hid::report::selector select,
                                            const std::span<uint8_t>& buffer) override
        {
            record({.what = event::kind::GET_REPORT,
                    .report_type = select.type(),
                    .report_id = select.id()});
            auto& app = instance();
            if (select == input_large::selector())
            {
                return {app.in_large.data(), sizeof(app.in_large)};
            }
            if (select == input_ext::selector())
            {
                return {app.in_ext.data(), sizeof(app.in_ext)};
            }
            if (select == feature_large::selector())
            {
                return {app.feat_large.data(), sizeof(app.feat_large)};
            }
            if (select == feature_ext::selector())
            {
                // reply through the transport provided buffer
                std::copy_n(app.feat_ext.data(), sizeof(app.feat_ext), buffer.begin());
                return buffer.first(sizeof(app.feat_ext));
            }
            // not handled: the device must reject the request
            return {};
        }

        void send_report_callback(hid::report::selector select, c2usb::result result) override
        {
            record({.what = event::kind::SEND_DONE,
                    .report_type = select.type(),
                    .report_id = select.id(),
                    .result = result.to_int()});
        }

        void suspend_callback(bool suspended) override
        {
            record({.what = suspended ? event::kind::SUSPEND : event::kind::RESUME});
        }

      private:
        std::array<uint8_t, sizeof(output_large)> output_rx_{};
        std::array<uint8_t, sizeof(feature_large)> feature_rx_{};
    };

    hid::session& start(const hid::session::params& params) override
    {
        record({.what = event::kind::START,
                .result = (params.transport != nullptr) ? 0 : -1,
                .channel = params.channel,
                .boot_mode = params.boot_protocol});
        return session_.emplace(params);
    }

    void stop(hid::session& sess) override
    {
        if (not session_.has_value() or (&sess != &session_.value()))
        {
            // stopping a foreign session
            isr_check_failed.store(true);
        }
        record({.what = event::kind::STOP});
        session_.reset();
    }

    session* active_session() { return session_.has_value() ? &session_.value() : nullptr; }

    // test controls and report contents
    std::atomic<bool> rearm{true};
    input_large in_large{};
    input_ext in_ext{};
    feature_large feat_large{};
    feature_ext feat_ext{};
    std::array<uint8_t, std::max(sizeof(output_large), sizeof(feature_large))> received{};

  private:
    recording_app()
        : hid::application(report_protocol())
    {}

    std::optional<session> session_{};
};

hid_host host;
recording_app& app = recording_app::instance();

event next_event(k_timeout_t timeout = K_MSEC(100))
{
    event ev{};
    zassert_ok(k_msgq_get(&events, &ev, timeout), "expected event not recorded");
    return ev;
}

event expect_event(event::kind what, k_timeout_t timeout = K_MSEC(100))
{
    auto ev = next_event(timeout);
    zassert_equal(ev.what, what, "unexpected event %u, expected %u", static_cast<unsigned>(ev.what),
                  static_cast<unsigned>(what));
    return ev;
}

void expect_no_event(k_timeout_t timeout = K_MSEC(20))
{
    event ev{};
    zassert_not_ok(k_msgq_get(&events, &ev, timeout), "unexpected event %u",
                   static_cast<unsigned>(ev.what));
}

void expect_report_event(event::kind what, type t, uint8_t id, uint16_t size = 0, int result = 0)
{
    auto ev = expect_event(what);
    zassert_equal(ev.report_type, t, "report type mismatch");
    zassert_equal(ev.report_id, id, "report ID %u, expected %u", ev.report_id, id);
    if (what == event::kind::SET_REPORT)
    {
        zassert_equal(ev.size, size, "report size %u, expected %u", ev.size, size);
    }
    if (what == event::kind::SEND_DONE)
    {
        zassert_equal(ev.result, result, "result %d, expected %d", ev.result, result);
    }
}

recording_app::session& active_session()
{
    auto* sess = app.active_session();
    zassert_not_null(sess, "no active session");
    return *sess;
}

template <typename T>
c2usb::result send(const T& report)
{
    return active_session().send_report(&report);
}

/// @brief  Reads the input register, as a host does on interrupt (wMaxInputLength bytes).
template <typename T>
void read_input_report(const T& expected)
{
    // the host always reads wMaxInputLength bytes
    std::array<uint8_t, sizeof(report_data<input_large>)> raw{};
    report_data<T> input{.length = 0};
    static_assert(sizeof(input) <= raw.size());

    zassert_true(hid_host::wait_int(true), "INT not asserted for input report");
    zassert_ok(hid_host::read(raw));
    std::ranges::copy(std::span(raw).first(sizeof(input)), as_writable_bytes(input).begin());
    zassert_equal(input.length, sizeof(input), "input report length %u",
                  static_cast<unsigned>(input.length));
    zassert_mem_equal(&input.report, &expected, sizeof(T), "input report content mismatch");
    zassert_true(hid_host::wait_int(false), "INT not deasserted after input report");
}

bool canary_intact(type t)
{
    return std::ranges::all_of(active_session().rx_buffer(t),
                               [](uint8_t b) { return b == CANARY; });
}

void* suite_setup()
{
    setup_bus();
    hid_device(app, product_info);
    return nullptr;
}

void before_each(void*)
{
    app.rearm.store(true);
    // enumerate the device as a host would, and wait for the session to start
    host.enumerate();
    event ev{};
    do
    {
        zassert_ok(k_msgq_get(&events, &ev, K_MSEC(100)), "session not started");
    } while (ev.what != event::kind::START);
    k_msgq_purge(&events);
    events_overflow.store(false);
    isr_check_failed.store(false);
}

void after_each(void*)
{
    // withdraw any report left pending by a failed test
    if (auto* sess = app.active_session(); sess != nullptr)
    {
        (void)sess->cancel_report_send();
    }
    zassert_false(events_overflow.load(), "event queue overflow");
    zassert_false(isr_check_failed.load(), "check failed in the I2C ISR context");
}
} // namespace

ZTEST_SUITE(i2c_hid_app, nullptr, suite_setup, before_each, after_each, nullptr);

ZTEST(i2c_hid_app, test_report_protocol)
{
    const auto& desc = host.descriptor();
    const auto& rp = app.report_info();

    // the maximal sizes cover the largest report of each type, including the report ID
    zassert_true(rp.uses_report_ids());
    zassert_equal(desc.wMaxInputLength, sizeof(report_data<input_large>));
    zassert_equal(desc.wMaxOutputLength, sizeof(report_data<output_large>));
    zassert_equal(rp.max_feature_size, sizeof(feature_large));
    zassert_equal(desc.wVendorID, product_info.vendor_id);
    zassert_equal(desc.wProductID, product_info.product_id);
    zassert_equal(desc.wVersionID, product_info.product_version);

    // the transfer buffer can be created from the report protocol or the report descriptor
    static_assert(sizeof(i2c::hid::device::make_buffer<recording_app::report_protocol>()) ==
                  i2c::hid::device::buffer_size(recording_app::report_protocol()));
    static_assert(sizeof(i2c::hid::device::make_buffer<recording_app::report_descriptor()>()) ==
                  sizeof(i2c::hid::device::make_buffer<recording_app::report_protocol>()));

    // the transfer buffer fits the largest host write
    zassert_true(i2c::hid::device::buffer_size(rp) >= sizeof(set_report_request<feature_ext>) -
                                                          sizeof(feature_ext) +
                                                          sizeof(feature_large));

    const auto expected = rp.descriptor.to_span();
    std::array<uint8_t, 160> rdesc{};
    zassert_true(expected.size() <= rdesc.size());
    zassert_equal(desc.wReportDescLength, expected.size());
    zassert_ok(i2c_write_read(controller, TEST_ADDRESS,
                              as_bytes(register_request{desc.wReportDescRegister}).data(),
                              sizeof(register_request), rdesc.data(), expected.size()));
    zassert_mem_equal(rdesc.data(), expected.data(), expected.size());
}

ZTEST(i2c_hid_app, test_session_lifecycle)
{
    auto* first = app.active_session();
    zassert_not_null(first);
    zassert_false(first->suspended());
    zassert_equal(first->protocol(), hid::protocol::REPORT);

    // a RESET command stops the session immediately, and starts a new one
    // when the host has read the reset response
    zassert_ok(host.command(i2c::hid::command<>(i2c::hid::opcode::RESET)));
    expect_event(event::kind::STOP);
    zassert_is_null(app.active_session(), "session not stopped by RESET");
    zassert_true(hid_host::wait_int(true));
    expect_no_event();

    le_uint16_t sentinel{0xaaaa};
    zassert_ok(hid_host::read(sentinel));
    zassert_equal(sentinel, 0);
    auto ev = expect_event(event::kind::START);
    zassert_equal(ev.channel, hid::channel::I2C);
    zassert_equal(ev.boot_mode, hid::boot::mode::NONE);
    zassert_equal(ev.result, 0, "session started without transport");
    zassert_not_null(app.active_session());
    zassert_equal(app.active_session()->channel(), hid::channel::I2C);
    zassert_false(hid_host::int_asserted());

    // only these state changes happened
    expect_no_event();
}

ZTEST(i2c_hid_app, test_suspend_resume)
{
    auto& sess = active_session();

    zassert_ok(host.command(i2c::hid::command<>(i2c::hid::opcode::SET_POWER, true)));
    expect_event(event::kind::SUSPEND);
    zassert_true(sess.suspended());

    // repeated commands don't change the state, no callback
    zassert_ok(host.command(i2c::hid::command<>(i2c::hid::opcode::SET_POWER, true)));
    expect_no_event();

    zassert_ok(host.command(i2c::hid::command<>(i2c::hid::opcode::SET_POWER, false)));
    expect_event(event::kind::RESUME);
    zassert_false(sess.suspended());

    // the session is kept through power state changes
    zassert_equal(app.active_session(), &sess);
}

ZTEST(i2c_hid_app, test_send_report_callback)
{
    fill(app.in_large, 0x10);
    zassert_equal(send(app.in_large), c2usb::result::ok);
    expect_no_event();
    read_input_report(app.in_large);
    expect_report_event(event::kind::SEND_DONE, type::INPUT, input_large::ID);
    expect_no_event();
}

ZTEST(i2c_hid_app, test_send_extended_id_input)
{
    // a shorter report than wMaxInputLength, with an ID above the short command limit
    fill(app.in_ext, 0x40);
    zassert_equal(send(app.in_ext), c2usb::result::ok);
    read_input_report(app.in_ext);
    expect_report_event(event::kind::SEND_DONE, type::INPUT, input_ext::ID);
}

ZTEST(i2c_hid_app, test_send_busy_and_cancel)
{
    input_large first{};
    fill(first, 0x20);
    fill(app.in_large, 0x30);

    zassert_equal(send(first), c2usb::result::ok);
    zassert_true(hid_host::wait_int(true));

    // a single report can be pending
    zassert_equal(send(app.in_large), c2usb::result::device_or_resource_busy);
    expect_no_event();

    // withdraw the pending report: the callback reports it, the interrupt is released
    zassert_equal(active_session().cancel_report_send(), c2usb::result::ok);
    expect_report_event(event::kind::SEND_DONE, type::INPUT, input_large::ID, 0,
                        c2usb::result(c2usb::result::operation_canceled).to_int());
    zassert_true(hid_host::wait_int(false), "INT not deasserted after cancel");

    // nothing to cancel
    zassert_equal(active_session().cancel_report_send(), c2usb::result::ok);
    expect_no_event();

    // the next report is accepted and delivered, instead of the cancelled one
    zassert_equal(send(app.in_large), c2usb::result::ok);
    read_input_report(app.in_large);
    expect_report_event(event::kind::SEND_DONE, type::INPUT, input_large::ID);

    // cancelling after delivery has no effect
    zassert_equal(active_session().cancel_report_send(), c2usb::result::ok);
    expect_no_event();
}

ZTEST(i2c_hid_app, test_large_output_report)
{
    output_large out{};
    fill(out, 0x50);
    zassert_ok(host.write_output(out));
    expect_report_event(event::kind::SET_REPORT, type::OUTPUT, output_large::ID, sizeof(out));
    zassert_mem_equal(app.received.data(), out.data(), sizeof(out));

    // the FEATURE receive buffer was not touched
    zassert_true(canary_intact(type::FEATURE), "OUTPUT write corrupted the FEATURE buffer");

    // also through the SET_REPORT command
    fill(out, 0x60);
    zassert_ok(host.set_report(out));
    expect_report_event(event::kind::SET_REPORT, type::OUTPUT, output_large::ID, sizeof(out));
    zassert_mem_equal(app.received.data(), out.data(), sizeof(out));
    zassert_true(canary_intact(type::FEATURE), "OUTPUT write corrupted the FEATURE buffer");
}

ZTEST(i2c_hid_app, test_large_feature_report)
{
    feature_large feat{};
    fill(feat, 0x70);
    zassert_ok(host.set_report(feat));
    expect_report_event(event::kind::SET_REPORT, type::FEATURE, feature_large::ID, sizeof(feat));
    zassert_mem_equal(app.received.data(), feat.data(), sizeof(feat));
    zassert_true(canary_intact(type::OUTPUT), "FEATURE write corrupted the OUTPUT buffer");

    fill(app.feat_large, 0x80);
    report_data<feature_large> response{.length = 0};
    zassert_ok(host.get_report(response));
    expect_report_event(event::kind::GET_REPORT, type::FEATURE, feature_large::ID);
    zassert_equal(response.length, sizeof(response));
    zassert_mem_equal(&response.report, &app.feat_large, sizeof(feature_large));
}

ZTEST(i2c_hid_app, test_extended_id_feature_report)
{
    // SET_REPORT and GET_REPORT in the extended command format
    feature_ext feat{};
    fill(feat, 0x90);
    zassert_ok(host.set_report(feat));
    expect_report_event(event::kind::SET_REPORT, type::FEATURE, feature_ext::ID, sizeof(feat));
    zassert_mem_equal(app.received.data(), feat.data(), sizeof(feat));

    // the application replies through the transport provided buffer
    fill(app.feat_ext, 0xa0);
    report_data<feature_ext> response{.length = 0};
    zassert_ok(host.get_report(response));
    expect_report_event(event::kind::GET_REPORT, type::FEATURE, feature_ext::ID);
    zassert_equal(response.length, sizeof(response));
    zassert_mem_equal(&response.report, &app.feat_ext, sizeof(feature_ext));
}

ZTEST(i2c_hid_app, test_get_input_reports)
{
    fill(app.in_large, 0xb0);
    report_data<input_large> large{.length = 0};
    zassert_ok(host.get_report(large));
    expect_report_event(event::kind::GET_REPORT, type::INPUT, input_large::ID);
    zassert_equal(large.length, sizeof(large));
    zassert_mem_equal(&large.report, &app.in_large, sizeof(input_large));

    fill(app.in_ext, 0xc0);
    report_data<input_ext> ext{.length = 0};
    zassert_ok(host.get_report(ext));
    expect_report_event(event::kind::GET_REPORT, type::INPUT, input_ext::ID);
    zassert_equal(ext.length, sizeof(ext));
    zassert_mem_equal(&ext.report, &app.in_ext, sizeof(input_ext));

    // GET_REPORT doesn't involve the interrupt line or the send feedback
    zassert_false(hid_host::int_asserted());
    expect_no_event();
}

ZTEST(i2c_hid_app, test_get_report_unhandled)
{
    // the application provides no data, the request is rejected with padding
    report_data<feature_unhandled> response{.length = 0};
    zassert_ok(host.get_report(response));
    expect_report_event(event::kind::GET_REPORT, type::FEATURE, feature_unhandled::ID);
    zassert_true(all_padding(as_bytes(response)), "unexpected data for unhandled report");

    // the device remains functional
    report_data<feature_ext> ext{.length = 0};
    zassert_ok(host.get_report(ext));
    expect_report_event(event::kind::GET_REPORT, type::FEATURE, feature_ext::ID);
    zassert_equal(ext.length, sizeof(ext));
}

ZTEST(i2c_hid_app, test_receive_flow_control)
{
    output_large out{};
    fill(out, 0xd0);

    // the application doesn't provide a new buffer after receiving a report
    app.rearm.store(false);
    zassert_ok(host.write_output(out));
    expect_report_event(event::kind::SET_REPORT, type::OUTPUT, output_large::ID, sizeof(out));

    // further reports of the type are dropped, other types are still received
    zassert_ok(host.write_output(out));
    expect_no_event();
    feature_ext feat{};
    zassert_ok(host.set_report(feat));
    expect_report_event(event::kind::SET_REPORT, type::FEATURE, feature_ext::ID, sizeof(feat));

    // once the buffer is provided again, reports are received
    active_session().arm(type::OUTPUT);
    fill(out, 0xe0);
    zassert_ok(host.write_output(out));
    expect_report_event(event::kind::SET_REPORT, type::OUTPUT, output_large::ID, sizeof(out));
    zassert_mem_equal(app.received.data(), out.data(), sizeof(out));
}

ZTEST(i2c_hid_app, test_invalid_writes)
{
    // report length field not matching the written data
    output_request<output_large> bad_length{host.descriptor().wOutputRegister};
    bad_length.data.length = sizeof(bad_length.data) - 1;
    zassert_ok(hid_host::write(bad_length));
    expect_no_event();

    // FEATURE report through the output register: too large for the OUTPUT buffer
    zassert_ok(host.write_output(feature_large{}));
    expect_no_event();
    zassert_true(canary_intact(type::OUTPUT), "oversized write corrupted the OUTPUT buffer");

    // write exceeding the device's transfer buffer: the excess is NACKed
    struct
    {
        output_request<feature_large> request;
        std::array<uint8_t, 16> excess{};
    } oversized{{host.descriptor().wOutputRegister}};
    zassert_not_ok(hid_host::write(oversized), "excess data was acknowledged");
    expect_no_event();

    // the device remains functional
    output_large out{};
    fill(out, 0xf0);
    zassert_ok(host.write_output(out));
    expect_report_event(event::kind::SET_REPORT, type::OUTPUT, output_large::ID, sizeof(out));
    zassert_mem_equal(app.received.data(), out.data(), sizeof(out));
}
