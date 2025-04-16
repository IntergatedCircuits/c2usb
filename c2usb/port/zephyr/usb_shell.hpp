#ifndef __USB_SHELL_HPP
#define __USB_SHELL_HPP

#if ((__has_include("zephyr/shell/shell.h")) && CONFIG_SHELL_BACKEND_C2USB)
#include <atomic>
#include <optional>
#include <zephyr/shell/shell.h>

#include "usb/df/class/cdc_acm.hpp"

namespace usb::zephyr
{
template <typename T>
class double_buffer
{
  public:
    using size_type = size_t;
    constexpr double_buffer(const std::span<T>& space)
        : buffer_{space.data()}, size_{space.size() / 2}
    {}

  protected:
    T* const buffer_{};
    const size_type size_{};

    T* buffer_of(uint8_t i) { return buffer_ + (i * size_); }
    auto size() const { return size_; }
};

class acm_rx_buffer : public double_buffer<uint8_t>
{
    using base = double_buffer<uint8_t>;
    using base::buffer_;
    using base::buffer_of;
    using base::size;
    using base::size_type;

    std::atomic<size_type> consume_pos_{};
    std::array<std::atomic<size_type>, 2> produce_pos_{};

  public:
    using base::base;
    void reset()
    {
        consume_pos_.store(0);
        produce_pos_[0].store(0);
        produce_pos_[1].store(0);
    }
    size_type read(uint8_t* data, size_type length);
    std::span<uint8_t> empty_side();
    bool set_produced(const std::span<uint8_t>& data);
};

class acm_tx_buffer : public double_buffer<uint8_t>
{
    using base = double_buffer<uint8_t>;
    using base::buffer_;
    using base::buffer_of;
    using base::size;
    using base::size_type;

    static constexpr size_type consuming_flag = std::numeric_limits<size_type>::max() / 2 + 1;
    std::array<std::atomic<size_type>, 2> pos_{};

  public:
    using base::base;
    void reset()
    {
        pos_[0].store(0);
        pos_[1].store(0);
    }
    void cancel_consume(const std::span<const uint8_t>& data);
    std::optional<std::span<const uint8_t>> write(const uint8_t* data, size_t* length);
    std::optional<std::span<const uint8_t>> advance(const std::span<const uint8_t>& data,
                                                    bool needs_zlp);
};

class usb_shell : public usb::df::cdc::acm::function
{
  public:
    static usb_shell& handle();

  private:
    usb_shell(const std::span<uint8_t>& tx_buffer, const std::span<uint8_t>& rx_buffer);
    ~usb_shell() override;

    void set_line(const line_config& cfg, line_event ev) override;
    void reset_line() override;
    void data_sent(const std::span<const uint8_t>& tx, bool needs_zlp) override;
    void data_received(const std::span<uint8_t>& rx) override;

    void receive_buffer_data();

    static int shell_tp_init(const ::shell_transport* transport, const void* config,
                             ::shell_transport_handler_t evt_handler, void* context);
    static int shell_tp_uninit(const ::shell_transport* transport);
    static int shell_tp_enable(const ::shell_transport* transport, bool blocking_tx);
    static int shell_tp_write(const ::shell_transport* transport, const void* data, size_t length,
                              size_t* cnt);
    static int shell_tp_read(const ::shell_transport* transport, void* data, size_t length,
                             size_t* cnt);
    static void shell_tp_update(const ::shell_transport* transport);
    static const ::shell_transport_api& shell_tp_api();

    void tp_handler(::shell_transport_evt evt)
    {
        if (tp_handler_)
        {
            tp_handler_(evt, shell_context_);
        }
    }
    void tx_done_handler() { tp_handler(::shell_transport_evt::SHELL_TRANSPORT_EVT_TX_RDY); }
    void rx_ready_handler() { tp_handler(::shell_transport_evt::SHELL_TRANSPORT_EVT_RX_RDY); }
    ::shell_transport_handler_t tp_handler_{};
    void* shell_context_{};
    acm_tx_buffer tx_buffer_;
    acm_rx_buffer rx_buffer_;
    std::atomic_bool active_{IS_ENABLED(CONFIG_SHELL_AUTOSTART)};
};

} // namespace usb::zephyr

#endif // ((__has_include("zephyr/shell/shell.h")) && CONFIG_SHELL_BACKEND_C2USB)

#endif // __USB_SHELL_HPP