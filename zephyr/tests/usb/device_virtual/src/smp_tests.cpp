#include "cdc_helpers.hpp"
#include "shell_test_commands.hpp"

#include <array>
#include <cstring>
#include <string_view>

#include <mgmt/mcumgr/transport/smp_internal.h>
#include <zephyr/kernel.h>
#include <zephyr/mgmt/mcumgr/grp/os_mgmt/os_mgmt.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/thread.hpp>
#include <zephyr/ztest.h>

#include <usb/df/vendor/zephyr/shell.hpp>
#if CONFIG_C2USB_MCUMGR_SMP_CONSOLE
#include <usb/df/vendor/zephyr/smp_console.hpp>
#endif

using namespace std::chrono_literals;
using namespace zephyr;

extern "C" const struct shell* c2usb_shell_handle();

struct serial_bytes
{
    std::array<uint8_t, 256> bytes{};
    size_t size{};
};

serial_bytes* serial_capture;

static int capture_serial_bytes(const void* data, int len)
{
    if (serial_capture == nullptr or len < 0 or
        serial_capture->size + static_cast<size_t>(len) > serial_capture->bytes.size())
    {
        return -ENOMEM;
    }
    std::memcpy(serial_capture->bytes.data() + serial_capture->size, data, len);
    serial_capture->size += len;
    return 0;
}

static serial_bytes make_echo_packet(std::string_view value, uint8_t op, uint8_t seq, char key,
                                     bool indefinite_map = false)
{
    std::array<uint8_t, 256> raw{};
    struct smp_hdr hdr{};
    size_t payload_size = value.size() + 4 + (value.size() > 23 ? 1 : 0) + indefinite_map;
    hdr.nh_len = sys_cpu_to_be16(payload_size);
    hdr.nh_flags = 0;
    hdr.nh_op = op;
    hdr.nh_group = sys_cpu_to_be16(MGMT_GROUP_ID_OS);
    hdr.nh_seq = seq;
    hdr.nh_id = OS_MGMT_ID_ECHO;
    hdr.nh_version = 1;
    std::memcpy(raw.data(), &hdr, sizeof(hdr));

    size_t offset = sizeof(hdr);
    raw[offset++] = indefinite_map ? 0xbf : 0xa1;
    raw[offset++] = 0x61;
    raw[offset++] = static_cast<uint8_t>(key);
    if (value.size() <= 23)
    {
        raw[offset++] = 0x60 | static_cast<uint8_t>(value.size());
    }
    else
    {
        raw[offset++] = 0x78;
        raw[offset++] = static_cast<uint8_t>(value.size());
    }
    std::memcpy(raw.data() + offset, value.data(), value.size());
    offset += value.size();
    if (indefinite_map)
    {
        raw[offset++] = 0xff;
    }

    serial_bytes result{};
    serial_capture = &result;
    int err = mcumgr_serial_tx_pkt(raw.data(), offset, capture_serial_bytes);
    serial_capture = nullptr;
    if (err != 0)
    {
        result.size = 0;
    }
    return result;
}

#if CONFIG_C2USB_SHELL_MCUMGR_TRANSPORT
ZTEST(c2usb_usb_device_virtual, test_smp_demux_boundaries_and_recovery)
{
    auto* dev = usb::test::host::wait_for_device();
    zassert_not_null(dev, "No USB device enumerated on virtual host");
    zassert_true(c2usb::device_virtual::configure_cdc_shell(dev),
                 "Failed to configure the CDC shell");
    zassert_true(usb::df::zephyr::shell::handle().get_line_config().data_terminal_ready(),
                 "CDC DTR was not enabled");
    zassert_true(::shell_ready(c2usb_shell_handle()), "Shell did not become ready");

    const uint8_t false_prefix = MCUMGR_SERIAL_HDR_PKT_1;
    int send_error = c2usb::device_virtual::send_bulk(dev, {&false_prefix, 1});
    zassert_equal(send_error, 0, "Failed to send split false prefix: %d", send_error);
    c2usb::device_virtual::reset_marker_command_count();
    zassert_equal(c2usb::device_virtual::send_text(dev, "c2usb_mark\n"), 0,
                  "Failed to send marker command");
    for (size_t attempt = 0;
         attempt < 100 and c2usb::device_virtual::get_marker_command_count() == 0; ++attempt)
    {
        this_thread::sleep_for(1ms);
    }
    zassert_equal(c2usb::device_virtual::get_marker_command_count(), 1,
                  "Shell did not dispatch the marker command");

    static std::array<uint8_t, 4096> output{};
    size_t output_size{};
    zassert_true(
        c2usb::device_virtual::read_until(dev, output, output_size, {}, 1, "C2USB_EDGE_MARK"),
        "False SMP prefix lost the following shell command");

    constexpr std::string_view payload = "mixed-rx";
    constexpr uint8_t sequence = 17;
    auto request = make_echo_packet(payload, MGMT_OP_WRITE, sequence, 'd');
    auto response = make_echo_packet(payload, MGMT_OP_WRITE_RSP, sequence, 'r', true);
    zassert_true(request.size > 0 and response.size > 0, "Failed to build serial echo frames");

    std::array<uint8_t, 64> first_transfer{};
    constexpr std::string_view marker_command = "c2usb_mark\n";
    std::memcpy(first_transfer.data(), marker_command.data(), marker_command.size());
    first_transfer[marker_command.size()] = request.bytes[0];
    zassert_equal(
        c2usb::device_virtual::send_bulk(dev, {first_transfer.data(), marker_command.size() + 1}),
        0, "Failed to send shell bytes and first SMP prefix byte");
    zassert_equal(c2usb::device_virtual::send_bulk(dev, {request.bytes.data() + 1, 1}), 0,
                  "Failed to split SMP prefix across USB transfers");

    std::array<uint8_t, 512> final_transfer{};
    const size_t request_tail = request.size - 2;
    std::memcpy(final_transfer.data(), request.bytes.data() + 2, request_tail);
    std::memcpy(final_transfer.data() + request_tail, marker_command.data(), marker_command.size());
    zassert_equal(c2usb::device_virtual::send_bulk(
                      dev, {final_transfer.data(), request_tail + marker_command.size()}),
                  0, "Failed to send SMP payload and trailing shell command");

    output_size = 0;
    zassert_true(c2usb::device_virtual::read_until(dev, output, output_size,
                                                   {response.bytes.data(), response.size}, 2,
                                                   "C2USB_EDGE_MARK"),
                 "Mixed shell/SMP input did not produce both shell commands and SMP response");

    static std::array<uint8_t, MCUMGR_SERIAL_MAX_FRAME + 32> oversized{};
    oversized[0] = MCUMGR_SERIAL_HDR_PKT_1;
    oversized[1] = MCUMGR_SERIAL_HDR_PKT_2;
    std::fill(oversized.begin() + 2, oversized.end() - 1, 'x');
    oversized.back() = '\n';
    zassert_equal(c2usb::device_virtual::send_bulk(dev, oversized), 0,
                  "Failed to send oversized SMP fragment");
    zassert_equal(c2usb::device_virtual::send_text(dev, "c2usb_mark\n"), 0,
                  "Failed to send marker after oversized SMP fragment");

    output_size = 0;
    zassert_true(
        c2usb::device_virtual::read_until(dev, output, output_size, {}, 1, "C2USB_EDGE_MARK"),
        "Shell stream did not recover after oversized SMP fragment");
}

ZTEST(c2usb_usb_device_virtual, test_smp_tx_backpressure_serializes_shell_writer)
{
    auto* dev = usb::test::host::wait_for_device();
    zassert_not_null(dev, "No USB device enumerated on virtual host");
    zassert_true(c2usb::device_virtual::configure_cdc_shell(dev),
                 "Failed to configure the CDC shell");
    zassert_true(usb::df::zephyr::shell::handle().get_line_config().data_terminal_ready(),
                 "CDC DTR was not enabled");
    zassert_true(::shell_ready(c2usb_shell_handle()), "Shell did not become ready");

    c2usb::device_virtual::reset_marker_command_count();
    int send_error = c2usb::device_virtual::send_text(dev, "c2usb_mark\n");
    zassert_equal(send_error, 0, "Failed to prime the shell backend: %d", send_error);
    for (size_t attempt = 0;
         attempt < 100 and c2usb::device_virtual::get_marker_command_count() == 0; ++attempt)
    {
        this_thread::sleep_for(1ms);
    }
    zassert_equal(c2usb::device_virtual::get_marker_command_count(), 1,
                  "Shell did not dispatch the marker command");
    static std::array<uint8_t, 4096> output{};
    size_t output_size{};
    zassert_true(
        c2usb::device_virtual::read_until(dev, output, output_size, {}, 1, "C2USB_EDGE_MARK"),
        "Shell backend did not become ready");

    const auto* sh = c2usb_shell_handle();
    bool shell_unlocked{};
    for (size_t attempt = 0; attempt < 1000; ++attempt)
    {
        if (k_sem_count_get(&sh->ctx->lock_sem) == 1)
        {
            shell_unlocked = true;
            break;
        }
        this_thread::sleep_for(1ms);
    }
    zassert_true(shell_unlocked, "Shell backend lock did not become available");

    std::array<char, 100> payload{};
    payload.fill('L');
    const std::string_view echo{payload.data(), payload.size()};
    constexpr uint8_t sequence = 23;
    auto request = make_echo_packet(echo, MGMT_OP_WRITE, sequence, 'd');
    auto response = make_echo_packet(echo, MGMT_OP_WRITE_RSP, sequence, 'r', true);
    zassert_true(request.size > 128 and response.size > 128,
                 "Echo frames must exceed the configured TX double buffer");
    zassert_equal(c2usb::device_virtual::send_bulk(dev, {request.bytes.data(), request.size}), 0,
                  "Failed to send long SMP echo request");

    bool smp_holds_shell_lock{};
    for (size_t attempt = 0; attempt < 2000; ++attempt)
    {
        if (k_sem_count_get(&sh->ctx->lock_sem) == 0)
        {
            smp_holds_shell_lock = true;
            break;
        }
        this_thread::sleep_for(1ms);
    }
    zassert_true(smp_holds_shell_lock, "SMP response did not acquire the shell TX lock");
    this_thread::sleep_for(30ms);
    zassert_equal(k_sem_count_get(&sh->ctx->lock_sem), 0,
                  "SMP response did not remain blocked by TX backpressure");

    zassert_equal(c2usb::device_virtual::send_text(dev, "c2usb_stress\n"), 0,
                  "Failed to queue concurrent shell output");

    output_size = 0;
    zassert_true(c2usb::device_virtual::read_until(dev, output, output_size,
                                                   {response.bytes.data(), response.size}, 1,
                                                   "C2USB_SHELL_END"),
                 "Timed out draining SMP and shell output");
    auto stream = std::span<const uint8_t>{output.data(), output_size};
    zassert_true(c2usb::device_virtual::count_text(stream, "C2USB_SHELL_BEGIN") == 1,
                 "Concurrent shell output was not delivered");
    auto frame_match =
        std::ranges::search(stream, std::span<const uint8_t>{response.bytes.data(), response.size});
    auto shell_match = std::ranges::search(stream, std::string_view{"C2USB_SHELL_BEGIN"});
    zassert_true(frame_match.begin() < shell_match.begin(),
                 "Shell output interleaved before the complete SMP serial response");
}
#endif

#if CONFIG_C2USB_MCUMGR_SMP_CONSOLE
ZTEST(c2usb_usb_device_virtual, test_smp_dedicated_cdc_echo)
{
    auto* dev = usb::test::host::wait_for_device();
    zassert_not_null(dev, "No USB device enumerated on virtual host");
    zassert_true(c2usb::device_virtual::configure_cdc_interface(dev, 0),
                 "Failed to configure the dedicated SMP CDC function");
    zassert_true(usb::df::zephyr::smp_console::handle().get_line_config().data_terminal_ready(),
                 "Dedicated SMP CDC DTR was not enabled");
    zassert_not_null(smp_client_transport_get(SMP_SERIAL_TRANSPORT),
                     "Dedicated CDC transport was not registered for SMP client use");

    std::array<char, 100> payload{};
    payload.fill('C');
    const std::string_view echo{payload.data(), payload.size()};
    constexpr uint8_t sequence = 41;
    auto request = make_echo_packet(echo, MGMT_OP_WRITE, sequence, 'd');
    auto response = make_echo_packet(echo, MGMT_OP_WRITE_RSP, sequence, 'r', true);
    zassert_true(request.size > 128 and response.size > 128,
                 "SMP echo frames must exceed the configured CDC TX double buffer");
    zassert_equal(c2usb::device_virtual::send_bulk(dev, {request.bytes.data(), request.size}), 0,
                  "Failed to send SMP request over dedicated CDC");

    std::array<uint8_t, 512> output{};
    size_t output_size{};
    zassert_true(c2usb::device_virtual::read_until(dev, output, output_size,
                                                   {response.bytes.data(), response.size}, 0,
                                                   "not-present"),
                 "Dedicated CDC did not return the SMP echo response");
}

ZTEST(c2usb_usb_device_virtual, test_smp_console_split_and_coalesced_frames)
{
    auto* dev = usb::test::host::wait_for_device();
    zassert_not_null(dev, "No USB device enumerated on virtual host");
    zassert_true(c2usb::device_virtual::configure_cdc_interface(dev, 0),
                 "Failed to configure the SMP console CDC function");

    static auto split_request = make_echo_packet("split", MGMT_OP_WRITE, 51, 'd');
    static auto split_response = make_echo_packet("split", MGMT_OP_WRITE_RSP, 51, 'r', true);
    zassert_true(split_request.size > 1 and split_response.size > 0,
                 "Failed to build split SMP echo frames");
    zassert_equal(
        c2usb::device_virtual::send_bulk(dev, {split_request.bytes.data(), split_request.size - 1}),
        0, "Failed to send SMP frame prefix");
    zassert_equal(c2usb::device_virtual::send_bulk(
                      dev, {split_request.bytes.data() + split_request.size - 1, 1}),
                  0, "Failed to send SMP frame delimiter separately");

    static std::array<uint8_t, 512> output{};
    size_t output_size{};
    zassert_true(c2usb::device_virtual::read_until(
                     dev, output, output_size, {split_response.bytes.data(), split_response.size},
                     0, "not-present"),
                 "SMP frame split before newline was not reassembled");

    static auto request_a = make_echo_packet("a", MGMT_OP_WRITE, 52, 'd');
    static auto response_a = make_echo_packet("a", MGMT_OP_WRITE_RSP, 52, 'r', true);
    static auto request_b = make_echo_packet("b", MGMT_OP_WRITE, 53, 'd');
    static auto response_b = make_echo_packet("b", MGMT_OP_WRITE_RSP, 53, 'r', true);
    zassert_true(request_a.size > 0 and request_b.size > 0 and response_a.size > 0 and
                     response_b.size > 0,
                 "Failed to build coalesced SMP echo frames");

    static std::array<uint8_t, 128> requests{};
    const size_t requests_size = request_a.size + request_b.size;
    zassert_true(requests_size <= requests.size(), "Coalesced SMP requests exceed test buffer");
    std::copy_n(request_a.bytes.begin(), request_a.size, requests.begin());
    std::copy_n(request_b.bytes.begin(), request_b.size, requests.begin() + request_a.size);
    zassert_equal(c2usb::device_virtual::send_bulk(dev, {requests.data(), requests_size}), 0,
                  "Failed to send coalesced SMP frames");

    static std::array<uint8_t, 128> responses{};
    const size_t responses_size = response_a.size + response_b.size;
    zassert_true(responses_size <= responses.size(), "Expected SMP responses exceed test buffer");
    std::copy_n(response_a.bytes.begin(), response_a.size, responses.begin());
    std::copy_n(response_b.bytes.begin(), response_b.size, responses.begin() + response_a.size);
    output_size = 0;
    zassert_true(c2usb::device_virtual::read_until(dev, output, output_size,
                                                   {responses.data(), responses_size}, 0,
                                                   "not-present"),
                 "Multiple SMP frames in one USB transfer were not processed");
}

ZTEST(c2usb_usb_device_virtual, test_smp_console_oversized_frame_recovery)
{
    auto* dev = usb::test::host::wait_for_device();
    zassert_not_null(dev, "No USB device enumerated on virtual host");
    zassert_true(c2usb::device_virtual::configure_cdc_interface(dev, 0),
                 "Failed to configure the SMP console CDC function");

    auto request = make_echo_packet("recovered", MGMT_OP_WRITE, 54, 'd');
    auto response = make_echo_packet("recovered", MGMT_OP_WRITE_RSP, 54, 'r', true);
    zassert_true(request.size > 0 and response.size > 0, "Failed to build recovery SMP frames");

    constexpr size_t oversized_size = MCUMGR_SERIAL_MAX_FRAME + 16;
    std::array<uint8_t, oversized_size + 128> input{};
    input[0] = MCUMGR_SERIAL_HDR_PKT_1;
    input[1] = MCUMGR_SERIAL_HDR_PKT_2;
    std::fill(input.begin() + 2, input.begin() + oversized_size - 1, 'x');
    input[oversized_size - 1] = '\n';
    std::copy_n(request.bytes.begin(), request.size, input.begin() + oversized_size);

    zassert_equal(
        c2usb::device_virtual::send_bulk(dev, {input.data(), oversized_size + request.size}), 0,
        "Failed to send oversized fragment and recovery request");

    std::array<uint8_t, 512> output{};
    size_t output_size{};
    zassert_true(c2usb::device_virtual::read_until(dev, output, output_size,
                                                   {response.bytes.data(), response.size}, 0,
                                                   "not-present"),
                 "SMP console did not recover after an oversized fragment");
}

ZTEST(c2usb_usb_device_virtual, test_smp_console_dtr_resets_partial_frame)
{
    auto* dev = usb::test::host::wait_for_device();
    zassert_not_null(dev, "No USB device enumerated on virtual host");
    zassert_true(c2usb::device_virtual::configure_cdc_interface(dev, 0),
                 "Failed to configure the SMP console CDC function");

    auto partial_request = make_echo_packet("partial", MGMT_OP_WRITE, 55, 'd');
    zassert_true(partial_request.size > 1, "Failed to build partial SMP request");
    zassert_equal(c2usb::device_virtual::send_bulk(
                      dev, {partial_request.bytes.data(), partial_request.size - 1}),
                  0, "Failed to send partial SMP request");
    zassert_true(c2usb::device_virtual::set_cdc_line_state(dev, 0, false),
                 "Failed to lower CDC DTR");
    zassert_true(c2usb::device_virtual::set_cdc_line_state(dev, 0, true),
                 "Failed to raise CDC DTR");

    auto request = make_echo_packet("after-dtr", MGMT_OP_WRITE, 56, 'd');
    auto response = make_echo_packet("after-dtr", MGMT_OP_WRITE_RSP, 56, 'r', true);
    zassert_true(request.size > 0 and response.size > 0, "Failed to build post-DTR SMP frames");
    zassert_equal(c2usb::device_virtual::send_bulk(dev, {request.bytes.data(), request.size}), 0,
                  "Failed to send SMP request after DTR reconnect");

    std::array<uint8_t, 512> output{};
    size_t output_size{};
    zassert_true(c2usb::device_virtual::read_until(dev, output, output_size,
                                                   {response.bytes.data(), response.size}, 0,
                                                   "not-present"),
                 "SMP console did not recover after DTR reset a partial frame");
}
#endif
