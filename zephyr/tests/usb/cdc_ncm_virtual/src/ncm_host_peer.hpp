#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <usb/class/cdc_ncm_ntb.hpp>
#include <usb_host.hpp>

class ncm_host_peer
{
  public:
    explicit ncm_host_peer(usb::test::device* dev);

    bool queue_datagram(std::span<const uint8_t> datagram);
    int transmit();
    int transmit_malformed(std::span<const uint8_t> datagram);
    int receive(std::span<std::span<const uint8_t>> datagrams, size_t& datagram_count,
                std::chrono::milliseconds timeout = std::chrono::milliseconds(3000),
                size_t* ntb_size = nullptr);
    int receive_notification(std::span<uint8_t> data, size_t& transferred,
                             std::chrono::milliseconds timeout = std::chrono::milliseconds(3000));

  private:
    int transmit_ntb(std::span<const uint8_t> ntb);

    static constexpr size_t NTB_WORD_COUNT = 1024;
    usb::test::device* dev_;
    std::array<uint32_t, NTB_WORD_COUNT> tx_buffer_{};
    std::array<uint32_t, NTB_WORD_COUNT> rx_buffer_{};
    usb::cdc::ncm::ntb_tx_ctx<16> tx_;
    usb::cdc::ncm::ntb_rx_ctx<16> rx_;
};
