// SPDX-License-Identifier: Apache-2.0
#ifndef __LEDS_SAVING_KEYBOARD_HPP_
#define __LEDS_SAVING_KEYBOARD_HPP_

#include "hid/application.hpp"
#include <hid/app/keyboard.hpp>
#include <hid/report_protocol.hpp>

/// @brief  An HID keyboard application that keeps your selected keyboard LED off.
///         Its purpose is to demonstrate USB communication without any user action.
template <std::uint8_t REPORT_ID = 0>
class leds_saving_keyboard : public hid::application
{
    using keys_report = hid::app::keyboard::keys_input_report<REPORT_ID>;
    using kb_leds_report = hid::app::keyboard::output_report<REPORT_ID>;

  public:
    leds_saving_keyboard(hid::page::keyboard_keypad key, hid::page::leds led)
        : hid::application(hid::report_protocol::from_descriptor<
                           hid::app::keyboard::app_report_descriptor<REPORT_ID>()>()),
          key_(key),
          led_(led)
    {}
    auto send_key(bool set)
    {
        keys_buffer_.scancodes.set(key_, set);
        return send_report(&keys_buffer_);
    }
    void start(hid::protocol prot) override
    {
        prot_ = prot;
        receive_report(&leds_buffer_);
    }
    void stop() override {}
    void set_report(hid::report::type, const std::span<const uint8_t>& data) override
    {
        auto* out_report = reinterpret_cast<const kb_leds_report*>(data.data());

        if (out_report->leds.test(led_))
        {
            send_key(true);
        }
        receive_report(&leds_buffer_);
    }
    void in_report_sent(const std::span<const uint8_t>&) override
    {
        if (keys_buffer_.scancodes.test(key_))
        {
            send_key(false);
        }
    }
    void get_report(hid::report::selector, const std::span<uint8_t>&) override
    {
        send_report(&keys_buffer_);
    }
    hid::protocol get_protocol() const override { return prot_; }

  private:
    C2USB_USB_TRANSFER_ALIGN(keys_report, keys_buffer_) {};
    const hid::page::keyboard_keypad key_{};
    const hid::page::leds led_{};
    C2USB_USB_TRANSFER_ALIGN(kb_leds_report, leds_buffer_) {};
    hid::protocol prot_{};
};

#endif // __LEDS_SAVING_KEYBOARD_HPP_
