#include "cdc_helpers.hpp"
#include "shell_test_commands.hpp"

#include <array>
#include <atomic>

#include <zephyr/shell/shell.h>
#include <zephyr/thread.hpp>
#include <zephyr/ztest.h>

#include <usb/df/vendor/zephyr/shell.hpp>

using namespace std::chrono_literals;
using namespace zephyr;

extern "C" const struct shell* c2usb_shell_handle();

namespace c2usb::device_virtual
{
std::atomic<unsigned> marker_command_count{};

void reset_marker_command_count()
{
    marker_command_count.store(0);
}

unsigned get_marker_command_count()
{
    return marker_command_count.load();
}
} // namespace c2usb::device_virtual

using namespace c2usb::device_virtual;

int c2usb_mark_command(const struct shell* sh, size_t argc, char** argv)
{
    marker_command_count.fetch_add(1);
    shell_print(sh, "C2USB_EDGE_MARK");
    return 0;
}
SHELL_CMD_REGISTER(c2usb_mark, NULL, "c2usb test marker", c2usb_mark_command);

int c2usb_stress_command(const struct shell* sh, size_t argc, char** argv)
{
    shell_print(sh, "C2USB_SHELL_BEGIN");
    for (unsigned i = 0; i < 32; ++i)
    {
        shell_fprintf(sh, SHELL_NORMAL, "C2USB_SHELL_DATA_%02u_0123456789abcdef\n", i);
    }
    shell_print(sh, "C2USB_SHELL_END");
    return 0;
}
SHELL_CMD_REGISTER(c2usb_stress, NULL, "c2usb TX backpressure test", c2usb_stress_command);

ZTEST(c2usb_usb_device_virtual, test_shell_cdc_command_round_trip)
{
    auto* dev = usb::test::host::wait_for_device();
    zassert_not_null(dev, "No USB device enumerated on virtual host");
    zassert_true(c2usb::device_virtual::configure_cdc_shell(dev),
                 "Failed to configure the CDC shell");
    zassert_true(usb::df::zephyr::shell::handle().get_line_config().data_terminal_ready(),
                 "CDC DTR was not enabled");
    zassert_true(::shell_ready(c2usb_shell_handle()), "Shell did not become ready");

    reset_marker_command_count();
    zassert_equal(c2usb::device_virtual::send_text(dev, "c2usb_mark\n"), 0,
                  "Failed to send marker command");
    for (size_t attempt = 0; attempt < 100 and get_marker_command_count() == 0; ++attempt)
    {
        this_thread::sleep_for(1ms);
    }
    zassert_equal(get_marker_command_count(), 1, "Shell did not dispatch the marker command");

    std::array<uint8_t, 512> output{};
    size_t output_size{};
    zassert_true(
        c2usb::device_virtual::read_until(dev, output, output_size, {}, 1, "C2USB_EDGE_MARK"),
        "Shell output was not received over CDC");
}

ZTEST(c2usb_usb_device_virtual, test_shell_cdc_command_split_across_transfers)
{
    auto* dev = usb::test::host::wait_for_device();
    zassert_not_null(dev, "No USB device enumerated on virtual host");
    zassert_true(c2usb::device_virtual::configure_cdc_shell(dev),
                 "Failed to configure the CDC shell");

    reset_marker_command_count();
    zassert_equal(c2usb::device_virtual::send_text(dev, "c2usb_"), 0,
                  "Failed to send first command fragment");
    zassert_equal(c2usb::device_virtual::send_text(dev, "mark\n"), 0,
                  "Failed to send second command fragment");
    for (size_t attempt = 0; attempt < 100 and get_marker_command_count() == 0; ++attempt)
    {
        this_thread::sleep_for(1ms);
    }
    zassert_equal(get_marker_command_count(), 1,
                  "Shell did not assemble the command across USB transfers");

    std::array<uint8_t, 512> output{};
    size_t output_size{};
    zassert_true(
        c2usb::device_virtual::read_until(dev, output, output_size, {}, 1, "C2USB_EDGE_MARK"),
        "Split shell command output was not received over CDC");
}
