// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "hid/application.hpp"
#include <hid/app/keyboard.hpp>
#include <hid/page/consumer.hpp>

using keyboard_leds_data = hid::app::keyboard::output_report<0>::leds_t;

class simple_keyboard : public hid::application
{
  public:
    using keys_report = hid::app::keyboard::keys_input_report<0>;
    using kb_leds_report = hid::app::keyboard::output_report<0>;

    struct attributes_report : public hid::report::base<hid::report::type::FEATURE, 0>
    {
        hid::app::keyboard::form_factor form_factor{};
        hid::app::keyboard::key_type key_type{};
        hid::app::keyboard::layout layout{};
        usb::istring ietf_lang_tag_index{};

        [[nodiscard]] static constexpr auto descriptor()
        {
            using namespace hid::page;
            using namespace hid::rdf;

            // clang-format off
            return hid::rdf::descriptor(
                usage_page<consumer>(),
                collection::logical(
                    conditional_report_id<0>(),
                    report_size(8),
                    report_count(4),
                    usage(consumer::KEYBOARD_FORM_FACTOR),
                    usage(consumer::KEYBOARD_KEY_TYPE),
                    usage(consumer::KEYBOARD_PHYSICAL_LAYOUT),
                    usage(consumer::KEYBOARD_IETF_LANGUAGE_TAG_INDEX),
                    logical_limits<1, 2>(0, std::numeric_limits<std::uint8_t>::max()),
                    feature::absolute_constant()
                )
            );
            // clang-format on
        }
    };
    attributes_report attributes{};

    class session : public hid::session
    {
      public:
        session(const hid::session::params& params)
            : hid::session(params)
        {
            receive_report(&leds_buffer_);
        }
        ~session() override
        {
            if (auto& cb = simple_keyboard::instance().leds_cbk_; cb != nullptr)
            {
                cb(keyboard_leds_data{});
            }
        }

        void set_report(hid::report::type type, const std::span<const uint8_t>& data) override
        {
            if ((type == kb_leds_report::type()) and (data.size() == sizeof(kb_leds_report)))
            {
                auto* out_report = reinterpret_cast<const kb_leds_report*>(data.data());
                if (auto& cb = simple_keyboard::instance().leds_cbk_; cb != nullptr)
                {
                    cb(out_report->leds);
                }
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
            if (select == attributes_report::selector())
            {
                return std::span<const uint8_t>(simple_keyboard::instance().attributes.data(),
                                                sizeof(attributes_report));
            }
            return {};
        }

      private:
        friend class simple_keyboard;
        alignas(std::uintptr_t) keys_report keys_buffer_{};
        alignas(std::uintptr_t) kb_leds_report leds_buffer_{};
    };

    static simple_keyboard& instance()
    {
        static simple_keyboard inst;
        return inst;
    }
    void set_leds_callback(void (*cb)(keyboard_leds_data)) { leds_cbk_ = cb; }
    c2usb::result send_key(hid::page::keyboard_keypad key, bool set)
    {
        if (!session_)
        {
            return c2usb::result::not_connected;
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
    static constexpr auto report_descriptor()
    {
        return hid::app::keyboard::app_report_descriptor<0>(attributes_report::descriptor());
    }
    simple_keyboard()
        : hid::application(hid::report_protocol::from_descriptor<report_descriptor()>())
    {}
    void (*leds_cbk_)(keyboard_leds_data){};
    std::optional<session> session_{};
};
