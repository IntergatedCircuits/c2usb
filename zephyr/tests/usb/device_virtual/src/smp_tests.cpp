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
