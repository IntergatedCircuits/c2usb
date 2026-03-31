// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "hid/application.hpp"
#include <hid/app/keyboard.hpp>

using keyboard_leds_data =
    hid::report_bitset<hid::page::leds, hid::page::leds::NUM_LOCK, hid::page::leds::KANA>;

template <void (*LedsCallback)(keyboard_leds_data), std::uint8_t REPORT_ID = 0>
class simple_keyboard : public hid::application
{
  public:
    using keys_report = hid::app::keyboard::keys_input_report<REPORT_ID>;
    using kb_leds_report = hid::app::keyboard::output_report<REPORT_ID>;

    class session : public hid::session
    {
      public:
        session(const hid::session::params& params)
            : hid::session(params)
        {
            LedsCallback(leds_buffer_.leds);
            receive_report(&leds_buffer_);
        }

        void set_report(hid::report::type type, const std::span<const uint8_t>& data) override
        {
            if ((type == kb_leds_report::type()) and (data.size() == sizeof(kb_leds_report)))
            {
                auto* out_report = reinterpret_cast<const kb_leds_report*>(data.data());
                LedsCallback(out_report->leds);
            }
            receive_report(&leds_buffer_);
        }
        std::span<const uint8_t>
        get_report(hid::report::selector select,
                   [[maybe_unused]] const std::span<uint8_t>& buffer) override
        {
            if (select == keys_report::selector())
            {
                return std::span<const uint8_t>(keys_buffer_.data(), sizeof(keys_buffer_));
            }
            if (select == kb_leds_report::selector())
            {
                return std::span<const uint8_t>(leds_buffer_.data(), sizeof(leds_buffer_));
            }
            return {};
        }

      private:
        friend class simple_keyboard;
        alignas(std::uintptr_t) keys_report keys_buffer_{};
        alignas(std::uintptr_t) kb_leds_report leds_buffer_{};
    };

    simple_keyboard()
        : hid::application(hid::report_protocol::from_descriptor<
                           hid::app::keyboard::app_report_descriptor<REPORT_ID>()>())
    {}
    c2usb::result send_key(hid::page::keyboard_keypad key, bool set)
    {
        if (!session_)
        {
            // work around compiler bug ( ICE when resolving such using enum members during template
            // instantiation (tsubst_copy at cp/pt.cc:17004))
            // return c2usb::result::not_connected;
            return c2usb::result::NO_CONNECTION;
        }
        // TODO: use alternating buffers when send rate is high
        session_->keys_buffer_.scancodes.set(key, set);
        return session_->send_report(&session_->keys_buffer_);
    }
    hid::session& start([[maybe_unused]] const hid::session::params& params) override
    {
        return session_.emplace(params);
    }
    void stop([[maybe_unused]] hid::session& sess) override { session_.reset(); }

  private:
    std::optional<session> session_{};
};
