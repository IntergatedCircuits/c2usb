// SPDX-License-Identifier: Apache-2.0
#ifndef __SIMPLE_KEYBOARD_HPP_
#define __SIMPLE_KEYBOARD_HPP_

#include "hid/application.hpp"
#include <etl/delegate.h>
#include <hid/app/keyboard.hpp>
#include <hid/report_protocol.hpp>

template <std::uint8_t REPORT_ID = 0>
class simple_keyboard : public hid::application
{
  public:
    using keys_report = hid::app::keyboard::keys_input_report<REPORT_ID>;
    using kb_leds_report = hid::app::keyboard::output_report<REPORT_ID>;

    using leds_callback = etl::delegate<void(const kb_leds_report&)>;

    simple_keyboard(leds_callback cbk)
        : hid::application(hid::report_protocol::from_descriptor<
                           hid::app::keyboard::app_report_descriptor<REPORT_ID>()>()),
          cbk_(cbk)
    {}
    auto send_key(hid::page::keyboard_keypad key, bool set)
    {
        // TODO: use alternating buffers when send rate is high
        keys_buffer_.scancodes.set(key, set);
        return send_report(&keys_buffer_);
    }
    void start(hid::protocol prot) override
    {
        prot_ = prot;
        receive_report(&leds_buffer_);
    }
    void stop() override {}
    void set_report([[maybe_unused]] hid::report::type type,
                    const std::span<const uint8_t>& data) override
    {
        auto* out_report = reinterpret_cast<const kb_leds_report*>(data.data());
        cbk_(*out_report);
        receive_report(&leds_buffer_);
    }
    void get_report([[maybe_unused]] hid::report::selector select,
                    [[maybe_unused]] const std::span<uint8_t>& buffer) override
    {
        send_report(&keys_buffer_);
    }
    // void in_report_sent([[maybe_unused]] const std::span<const uint8_t>& data) override {}
    hid::protocol get_protocol() const override { return prot_; }

    void set_report_protocol(const hid::report_protocol& rp)
    {
        assert(!has_transport());
        report_info_ = rp;
    }

  private:
    alignas(std::uintptr_t) keys_report keys_buffer_{};
    alignas(std::uintptr_t) kb_leds_report leds_buffer_{};
    leds_callback cbk_;
    hid::protocol prot_{};
};

#endif // __SIMPLE_KEYBOARD_HPP_
