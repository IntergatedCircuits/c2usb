#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <usb/class/cdc.hpp>
#include <usb_host.hpp>

namespace c2usb::device_virtual
{
bool configure_cdc_shell(usb::test::device* dev);
int send_bulk(usb::test::device* dev, std::span<const uint8_t> data);
int send_text(usb::test::device* dev, std::string_view text);
size_t count_text(std::span<const uint8_t> bytes, std::string_view text);
bool read_until(usb::test::device* dev, std::span<uint8_t> output, size_t& output_size,
                std::span<const uint8_t> expected, size_t marker_count, std::string_view marker);
} // namespace c2usb::device_virtual
