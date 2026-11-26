// SPDX-License-Identifier: Apache-2.0
/*
 * I2C HID loopback tests with the simple_keyboard application:
 * typical keyboard use, and the Linux i2c-hid driver's device bring-up sequence.
 */
#include "i2c_hid_host.hpp"

#include <atomic>

#include <hid/example/simple_keyboard.hpp>

using keyboard_keypad = hid::page::keyboard_keypad;
using namespace test;

namespace
{
constexpr i2c::hid::product_info product_info{0x2fe3, 0x1202, i2c::hid::version("1.0")};

K_SEM_DEFINE(leds_sem, 0, 1);
std::atomic<uint8_t> last_leds{};

using keys_report = simple_keyboard::keys_report;
using leds_report = simple_keyboard::kb_leds_report;
using attributes_report = simple_keyboard::attributes_report;

hid_host host;

/// @brief  Sends a key state change from the application side, the session is created
///         asynchronously in the target ISR at reset completion.
c2usb::result send_key(keyboard_keypad key, bool pressed)
{
    k_timepoint_t end = sys_timepoint_calc(K_MSEC(10));
    auto result = simple_keyboard::instance().send_key(key, pressed);
    while ((result == c2usb::result::not_connected) and !sys_timepoint_expired(end))
    {
        k_sleep(K_TICKS(1));
        result = simple_keyboard::instance().send_key(key, pressed);
    }
    return result;
}

/// @brief  Waits for the input report triggered by the application, reads it and checks
///         the key's state in it.
void read_key_report(keyboard_keypad key, bool pressed)
{
    report_data<keys_report> input{.length = 0};

    zassert_true(hid_host::wait_int(true), "INT not asserted for input report");
    zassert_ok(hid_host::read(input));
    zassert_equal(input.length, sizeof(input), "unexpected input report length %u",
                  static_cast<unsigned>(input.length));
    zassert_true(hid_host::wait_int(false), "INT not deasserted after input report");
    zassert_equal(input.report.scancodes.test(key), pressed, "key 0x%02x state mismatch",
                  static_cast<unsigned>(key));
}

void leds_callback(keyboard_leds_data leds)
{
    last_leds.store(leds.test(hid::page::leds::CAPS_LOCK) ? 1 : 0);
    k_sem_give(&leds_sem);
}

leds_report caps_lock_leds(bool on)
{
    leds_report report{};
    report.leds.set(hid::page::leds::CAPS_LOCK, on);
    return report;
}

void* suite_setup()
{
    setup_bus();
    simple_keyboard::instance().set_leds_callback(leds_callback);
    hid_device(simple_keyboard::instance(), product_info);
    return nullptr;
}

void before_each(void*)
{
    // enumerate the device as a host would
    host.enumerate();
    k_sem_reset(&leds_sem);
}

void after_each(void*)
{
    // leave no key pressed behind, which would show up in the next test's first report
    if (simple_keyboard::instance().send_key(keyboard_keypad::KEYBOARD_CAPS_LOCK, false) ==
        c2usb::result::ok)
    {
        if (hid_host::wait_int(true))
        {
            report_data<keys_report> input{};
            (void)hid_host::read(input);
        }
    }
}
} // namespace

ZTEST_SUITE(i2c_hid_loopback, nullptr, suite_setup, before_each, after_each, nullptr);

ZTEST(i2c_hid_loopback, test_hid_descriptor)
{
    const auto& desc = host.descriptor();
    const auto& rp = simple_keyboard::instance().report_info();

    zassert_equal(desc.wHIDDescLength, sizeof(i2c::hid::descriptor));
    zassert_equal(desc.bcdVersion, i2c::hid::SPEC_VERSION);
    zassert_equal(desc.wReportDescLength, rp.descriptor.size());
    zassert_equal(desc.wMaxInputLength, sizeof(report_data<keys_report>));
    zassert_equal(desc.wMaxOutputLength, sizeof(report_data<leds_report>));
    zassert_equal(desc.wVendorID, product_info.vendor_id);
    zassert_equal(desc.wProductID, product_info.product_id);
    zassert_equal(desc.wVersionID, product_info.product_version);

    // registers must be distinct, and differ from the HID descriptor register
    const std::array<uint16_t, 6> regs{HID_DESC_REGISTER,     desc.wReportDescRegister,
                                       desc.wInputRegister,   desc.wOutputRegister,
                                       desc.wCommandRegister, desc.wDataRegister};
    for (size_t i = 0; i < regs.size(); ++i)
    {
        for (size_t j = i + 1; j < regs.size(); ++j)
        {
            zassert_not_equal(regs[i], regs[j], "register %zu and %zu collide", i, j);
        }
    }
}

ZTEST(i2c_hid_loopback, test_report_descriptor)
{
    const auto& desc = host.descriptor();
    const auto expected = simple_keyboard::instance().report_info().descriptor.to_span();

    std::array<uint8_t, 128> buf{};
    zassert_true(expected.size() <= buf.size());
    zassert_ok(i2c_write_read(controller, TEST_ADDRESS,
                              as_bytes(register_request{desc.wReportDescRegister}).data(),
                              sizeof(register_request), buf.data(), expected.size()));
    zassert_mem_equal(buf.data(), expected.data(), expected.size());
}

ZTEST(i2c_hid_loopback, test_repeated_reset)
{
    // before_each() has already done a full reset
    for (int i = 0; i < 3; ++i)
    {
        host.reset();
    }
}

ZTEST(i2c_hid_loopback, test_input_report)
{
    zassert_equal(send_key(keyboard_keypad::KEYBOARD_CAPS_LOCK, true), c2usb::result::ok);
    read_key_report(keyboard_keypad::KEYBOARD_CAPS_LOCK, true);

    zassert_equal(send_key(keyboard_keypad::KEYBOARD_CAPS_LOCK, false), c2usb::result::ok);
    read_key_report(keyboard_keypad::KEYBOARD_CAPS_LOCK, false);

    zassert_false(hid_host::int_asserted(), "INT asserted without pending report");
}

ZTEST(i2c_hid_loopback, test_input_report_busy)
{
    // a single report can be pending, until the host reads it
    zassert_equal(send_key(keyboard_keypad::KEYBOARD_CAPS_LOCK, true), c2usb::result::ok);
    zassert_true(hid_host::wait_int(true));
    zassert_equal(simple_keyboard::instance().send_key(keyboard_keypad::KEYBOARD_ENTER, true),
                  c2usb::result::device_or_resource_busy,
                  "second report accepted while the first is pending");

    // the pending report is delivered, the refused one isn't sent
    // (its key state is still recorded in the keyboard's report buffer)
    read_key_report(keyboard_keypad::KEYBOARD_CAPS_LOCK, true);
    zassert_false(hid_host::int_asserted(), "refused report was queued");

    // once delivered, the next report is accepted
    zassert_equal(send_key(keyboard_keypad::KEYBOARD_ENTER, false), c2usb::result::ok);
    read_key_report(keyboard_keypad::KEYBOARD_ENTER, false);
}

ZTEST(i2c_hid_loopback, test_input_read_beyond_report)
{
    // reading more than the report, the device sends padding until the controller NACKs
    struct
    {
        report_data<keys_report> input{.length = 0};
        std::array<uint8_t, 12> padding{};
    } oversized_read;

    zassert_equal(send_key(keyboard_keypad::KEYBOARD_ENTER, true), c2usb::result::ok);
    zassert_true(hid_host::wait_int(true));
    zassert_ok(hid_host::read(oversized_read));
    zassert_equal(oversized_read.input.length, sizeof(oversized_read.input));
    zassert_true(oversized_read.input.report.scancodes.test(keyboard_keypad::KEYBOARD_ENTER));
    zassert_true(std::ranges::all_of(oversized_read.padding, [](uint8_t b) { return b == 0xff; }),
                 "unexpected padding content");
    zassert_true(hid_host::wait_int(false));

    // the device remains functional
    zassert_equal(send_key(keyboard_keypad::KEYBOARD_ENTER, false), c2usb::result::ok);
    read_key_report(keyboard_keypad::KEYBOARD_ENTER, false);
}

ZTEST(i2c_hid_loopback, test_output_report)
{
    zassert_ok(host.write_output(caps_lock_leds(true)));
    zassert_ok(k_sem_take(&leds_sem, K_MSEC(100)), "output report not received");
    zassert_equal(last_leds.load(), 1);

    zassert_ok(host.write_output(caps_lock_leds(false)));
    zassert_ok(k_sem_take(&leds_sem, K_MSEC(100)), "output report not received");
    zassert_equal(last_leds.load(), 0);
}

ZTEST(i2c_hid_loopback, test_set_report_command)
{
    zassert_ok(host.set_report(caps_lock_leds(true)));
    zassert_ok(k_sem_take(&leds_sem, K_MSEC(100)), "SET_REPORT not received");
    zassert_equal(last_leds.load(), 1);
}

ZTEST(i2c_hid_loopback, test_get_report_command)
{
    // input report, nothing pressed
    report_data<keys_report> input{.length = 0};
    input.report.scancodes.set(keyboard_keypad::KEYBOARD_A);
    zassert_ok(host.get_report(input));
    zassert_equal(input.length, sizeof(input));
    const keys_report no_keys{};
    zassert_mem_equal(&input.report, &no_keys, sizeof(keys_report),
                      "unexpected input report content");

    // feature report: keyboard attributes
    auto& attrs = simple_keyboard::instance().attributes;
    attrs.layout = hid::app::keyboard::layout::_102;
    report_data<attributes_report> feature{.length = 0};
    zassert_ok(host.get_report(feature));
    zassert_equal(feature.length, sizeof(feature));
    zassert_mem_equal(&feature.report, &attrs, sizeof(attributes_report));
}

ZTEST(i2c_hid_loopback, test_set_power)
{
    zassert_ok(host.command(i2c::hid::command<0>(i2c::hid::opcode::SET_POWER, true)));  // sleep
    zassert_ok(host.command(i2c::hid::command<0>(i2c::hid::opcode::SET_POWER, false))); // on

    zassert_equal(send_key(keyboard_keypad::KEYBOARD_CAPS_LOCK, true), c2usb::result::ok);
    read_key_report(keyboard_keypad::KEYBOARD_CAPS_LOCK, true);
}

ZTEST(i2c_hid_loopback, test_invalid_register_read)
{
    // an unknown register is rejected, the device sends padding instead of data
    std::array<uint8_t, 4> buf{};
    zassert_ok(host.read_register(0x1234, buf));
    zassert_true(std::ranges::all_of(buf, [](uint8_t b) { return b == 0xff; }),
                 "unexpected data from invalid register");

    // the device remains functional
    i2c::hid::descriptor desc{};
    zassert_ok(host.read_hid_descriptor(desc));
    zassert_equal(desc.wHIDDescLength, sizeof(i2c::hid::descriptor));
}

ZTEST(i2c_hid_loopback, test_other_address_nack)
{
    le_uint16_t buf{};
    zassert_not_ok(
        i2c_read(controller, as_writable_bytes(buf).data(), sizeof(buf), TEST_ADDRESS + 1),
        "transfer to another address was acknowledged");

    // the device remains functional
    i2c::hid::descriptor desc{};
    zassert_ok(host.read_hid_descriptor(desc));
}

ZTEST(i2c_hid_loopback, test_stress)
{
    constexpr int ITERATIONS = 200;
    for (int i = 0; i < ITERATIONS; ++i)
    {
        zassert_equal(send_key(keyboard_keypad::KEYBOARD_CAPS_LOCK, true), c2usb::result::ok);
        read_key_report(keyboard_keypad::KEYBOARD_CAPS_LOCK, true);
        zassert_equal(send_key(keyboard_keypad::KEYBOARD_CAPS_LOCK, false), c2usb::result::ok);
        read_key_report(keyboard_keypad::KEYBOARD_CAPS_LOCK, false);

        // interleave controller initiated transfers
        if ((i % 16) == 0)
        {
            i2c::hid::descriptor desc{};
            zassert_ok(host.read_hid_descriptor(desc));
            zassert_equal(desc.wVendorID, product_info.vendor_id);
        }
    }
}

/*
 * Full device bring-up, following the sequence of the Linux i2c-hid driver
 * (drivers/hid/i2c-hid/i2c-hid-core.c) at probe and when the HID device is opened.
 * This suite has no per-test reset, the device is taken in whatever state it's in,
 * as at a driver (re)bind.
 */
ZTEST_SUITE(i2c_hid_linux_boot, nullptr, suite_setup, nullptr, nullptr, nullptr);

ZTEST(i2c_hid_linux_boot, test_boot_sequence)
{
    // i2c_hid_probe_address(): presence check with a single byte read (i2c_smbus_read_byte()),
    // the device answers with the empty input register
    uint8_t presence = 0xaa;
    zassert_ok(i2c_read(controller, &presence, sizeof(presence), TEST_ADDRESS),
               "device not present on the bus");
    zassert_equal(presence, 0, "unexpected presence check data");

    // i2c_hid_fetch_hid_descriptor(): read and validate the HID descriptor
    i2c::hid::descriptor desc{};
    zassert_ok(host.read_hid_descriptor(desc));
    zassert_equal(desc.bcdVersion, i2c::hid::SPEC_VERSION, "unsupported HID descriptor version");
    zassert_equal(desc.wHIDDescLength, sizeof(i2c::hid::descriptor),
                  "unexpected HID descriptor length");

    // i2c_hid_start_hwreset(): power on, then reset
    zassert_ok(host.command(i2c::hid::command<0>(i2c::hid::opcode::SET_POWER, false)));
    zassert_ok(host.command(i2c::hid::command<0>(i2c::hid::opcode::RESET)));

    // i2c_hid_finish_hwreset(): the reset completes when the interrupt handler
    // reads a zero length input (i2c_hid_get_input() always reads wMaxInputLength)
    zassert_true(hid_host::wait_int(true), "INT not asserted after RESET");
    struct
    {
        le_uint16_t length{0xaaaa};
        std::array<uint8_t, sizeof(keys_report)> rest{};
    } reset_response;
    zassert_equal(sizeof(reset_response), desc.wMaxInputLength);
    zassert_ok(hid_host::read(reset_response));
    zassert_equal(reset_response.length, 0, "invalid reset response");
    zassert_true(hid_host::wait_int(false), "INT not deasserted after reset response");

    // i2c_hid_parse(): read the report descriptor
    const auto expected = simple_keyboard::instance().report_info().descriptor.to_span();
    std::array<uint8_t, 128> rdesc{};
    zassert_true(desc.wReportDescLength <= rdesc.size());
    zassert_equal(desc.wReportDescLength, expected.size());
    zassert_ok(i2c_write_read(controller, TEST_ADDRESS,
                              as_bytes(register_request{desc.wReportDescRegister}).data(),
                              sizeof(register_request), rdesc.data(), desc.wReportDescLength));
    zassert_mem_equal(rdesc.data(), expected.data(), expected.size());

    // hid core initializes the feature reports with GET_REPORT
    report_data<attributes_report> feature{.length = 0};
    zassert_ok(host.get_report(feature));
    zassert_equal(feature.length, sizeof(feature));
    zassert_mem_equal(&feature.report, &simple_keyboard::instance().attributes,
                      sizeof(attributes_report));

    // i2c_hid_open() -> hid-input syncs the keyboard LED state via the output register
    k_sem_reset(&leds_sem);
    zassert_ok(host.write_output(caps_lock_leds(false)));
    zassert_ok(k_sem_take(&leds_sem, K_MSEC(100)), "LED output report not received");
    zassert_equal(last_leds.load(), 0);

    // the interrupt handler reads input reports
    zassert_false(hid_host::int_asserted(), "INT asserted without pending report");
    zassert_equal(send_key(keyboard_keypad::KEYBOARD_CAPS_LOCK, true), c2usb::result::ok);
    read_key_report(keyboard_keypad::KEYBOARD_CAPS_LOCK, true);

    // the host reacts with an LED update
    zassert_ok(host.write_output(caps_lock_leds(true)));
    zassert_ok(k_sem_take(&leds_sem, K_MSEC(100)), "LED output report not received");
    zassert_equal(last_leds.load(), 1);

    zassert_equal(send_key(keyboard_keypad::KEYBOARD_CAPS_LOCK, false), c2usb::result::ok);
    read_key_report(keyboard_keypad::KEYBOARD_CAPS_LOCK, false);

    // i2c_hid_core_suspend() / i2c_hid_core_resume(): power state changes
    zassert_ok(host.command(i2c::hid::command<0>(i2c::hid::opcode::SET_POWER, true)));
    zassert_ok(host.command(i2c::hid::command<0>(i2c::hid::opcode::SET_POWER, false)));
    zassert_equal(send_key(keyboard_keypad::KEYBOARD_A, true), c2usb::result::ok);
    read_key_report(keyboard_keypad::KEYBOARD_A, true);
    zassert_equal(send_key(keyboard_keypad::KEYBOARD_A, false), c2usb::result::ok);
    read_key_report(keyboard_keypad::KEYBOARD_A, false);

    zassert_false(hid_host::int_asserted(), "INT asserted without pending report");
}
