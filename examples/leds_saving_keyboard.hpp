/// @file
///
/// @author Benedek Kupper
/// @date   2023
///
/// @copyright
///         This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
///         If a copy of the MPL was not distributed with this file, You can obtain one at
///         https://mozilla.org/MPL/2.0/.
///
#ifndef __LEDS_SAVING_KEYBOARD_HPP_
#define __LEDS_SAVING_KEYBOARD_HPP_

#include <hid/app/keyboard.hpp>
#include <hid/report_protocol.hpp>

#include "hid/application.hpp"

/// @brief  An HID keyboard application that keeps your selected keyboard LED off.
///         Its purpose is to demonstrate USB communication without any user action.
class leds_saving_keyboard : public hid::application
{
    using keys_report = hid::app::keyboard::keys_input_report<0>;
    using kb_leds_report = hid::app::keyboard::output_report<0>;

    static const auto& report_prot()
    {
        static constexpr auto rd{hid::app::keyboard::app_report_descriptor<0>()};
        static constexpr hid::report_protocol rp{rd};
        return rp;
    }

  public:
    leds_saving_keyboard(hid::page::keyboard_keypad key, hid::page::leds led)
        : hid::application(report_prot()),
          key_(static_cast<uint8_t>(key)),
          led_mask_(1 << (static_cast<uint8_t>(led) - 1))
    {}
    void start(hid::protocol prot) override
    {
        prot_ = prot;
        receive_report(&leds_buffer_);
    }
    void stop() override {}
    void set_report(hid::report::type type, const std::span<const uint8_t>& data) override
    {
        auto* out_report = reinterpret_cast<const kb_leds_report*>(data.data());

        if ((out_report->leds & led_mask_) != 0)
        {
            keys_buffer_.scancodes[0] = key_;
            send_report(&keys_buffer_);
        }
        receive_report(&leds_buffer_);
    }
    void in_report_sent(const std::span<const uint8_t>& data) override
    {
        if (keys_buffer_.scancodes[0] == key_)
        {
            keys_buffer_.scancodes[0] = 0;
            send_report(&keys_buffer_);
        }
    }
    void get_report(hid::report::selector select, const std::span<uint8_t>& buffer) override
    {
        send_report(&keys_buffer_);
    }
    hid::protocol get_protocol() const override { return prot_; }

  private:
    C2USB_USB_TRANSFER_ALIGN(keys_report, keys_buffer_){};
    const uint8_t key_{};
    const uint8_t led_mask_{};
    C2USB_USB_TRANSFER_ALIGN(kb_leds_report, leds_buffer_){};
    hid::protocol prot_{};
};

#endif // __LEDS_SAVING_KEYBOARD_HPP_
