// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "hid/application.hpp"
#include <bitfilled/integer.hpp>
#include <etl/alignment.h>
#include <hid/app/mouse.hpp>

template <void (*ResolutionCallback)(uint8_t, uint8_t),
          // Linux only works if a report ID is used, see
          // https://bugzilla.kernel.org/show_bug.cgi?id=220144
          std::uint8_t MOUSE_REPORT_ID = 1, std::int16_t AXIS_LIMIT = 127,
          int16_t WHEEL_LIMIT = 32767>
class high_resolution_mouse : public hid::application
{
    static constexpr auto LAST_BUTTON = hid::page::button(3);
    static constexpr int16_t MAX_SCROLL_RESOLUTION = 120;

  public:
    template <uint8_t REPORT_ID = 0>
    struct mouse_report_base : public hid::report::base<hid::report::type::INPUT, REPORT_ID>
    {
        hid::report_bitset<hid::page::button, hid::page::button(1), LAST_BUTTON> buttons{};
        bitfilled::packed_integer<std::endian::little, bitfilled::byte_width(AXIS_LIMIT), int> x{};
        bitfilled::packed_integer<std::endian::little, bitfilled::byte_width(AXIS_LIMIT), int> y{};
        bitfilled::packed_integer<std::endian::little, bitfilled::byte_width(WHEEL_LIMIT), int>
            wheel_y{};
        bitfilled::packed_integer<std::endian::little, bitfilled::byte_width(WHEEL_LIMIT), int>
            wheel_x{};

        bool operator==(const mouse_report_base& other) const = default;
        bool operator!=(const mouse_report_base& other) const = default;

        bool steady() const { return (x == 0) && (y == 0) && (wheel_y == 0) && (wheel_x == 0); }
    };
    using mouse_report = mouse_report_base<MOUSE_REPORT_ID>;
    using resolution_multiplier_report =
        hid::app::mouse::resolution_multiplier_report<MAX_SCROLL_RESOLUTION, MOUSE_REPORT_ID>;

    class session : public hid::session
    {
      public:
        session(const hid::session::params& params)
            : hid::session(params)
        {
            ResolutionCallback(1, 1);
            receive_report(&multiplier_report_);
        }

        void set_report(hid::report::type type, const std::span<const uint8_t>& data) override
        {
            if ((type == resolution_multiplier_report::type()) and
                (data.size() == sizeof(resolution_multiplier_report)))
            {
                multiplier_report_ =
                    *reinterpret_cast<const resolution_multiplier_report*>(data.data());
                ResolutionCallback(multiplier_report_.vertical_scroll_multiplier(),
                                   multiplier_report_.horizontal_scroll_multiplier());
            }
            receive_report(&multiplier_report_);
        }
        std::span<const uint8_t>
        get_report(hid::report::selector select,
                   [[maybe_unused]] const std::span<uint8_t>& buffer) override
        {
            if (select == in_report_.selector())
            {
                return std::span<const uint8_t>(in_report_.data(), sizeof(in_report_));
            }
            if (select == multiplier_report_.selector())
            {
                return std::span<const uint8_t>(multiplier_report_.data(),
                                                sizeof(multiplier_report_));
            }
            return {};
        }
        const auto& multiplier_report() const { return multiplier_report_; }

      private:
        alignas(std::uintptr_t) mouse_report in_report_{};
        alignas(std::uintptr_t) resolution_multiplier_report multiplier_report_{};
    };

    static constexpr auto report_desc()
    {
        using namespace hid::page;
        using namespace hid::rdf;

        // clang-format off
        return descriptor(
            usage_page<generic_desktop>(),
            usage(generic_desktop::MOUSE),
            collection::application(
                conditional_report_id<MOUSE_REPORT_ID>(),
                usage(generic_desktop::POINTER),
                collection::physical(
                    // buttons
                    usage_extended_limits(button(1), LAST_BUTTON),
                    logical_limits<1, 1>(0, 1),
                    report_count(static_cast<uint8_t>(LAST_BUTTON)),
                    report_size(1),
                    input::absolute_variable(),
                    input::byte_padding<static_cast<uint8_t>(LAST_BUTTON)>(),

                    // relative X,Y directions
                    usage(generic_desktop::X),
                    usage(generic_desktop::Y),
                    logical_limits<(AXIS_LIMIT > std::numeric_limits<int8_t>::max() ? 2 : 1)>(-AXIS_LIMIT, AXIS_LIMIT),
                    report_count(2),
                    report_size(AXIS_LIMIT > std::numeric_limits<int8_t>::max() ? 16 : 8),
                    input::relative_variable(),

                    hid::app::mouse::high_resolution_scrolling<WHEEL_LIMIT, MAX_SCROLL_RESOLUTION>()
                )
            )
        );
        // clang-format on
    }

    high_resolution_mouse()
        : hid::application(hid::report_protocol::from_descriptor<report_desc()>())
    {}

    c2usb::result send_report(const mouse_report& report)
    {
        if (!session_)
        {
            // work around compiler bug ( ICE when resolving such using enum members during template
            // instantiation (tsubst_copy at cp/pt.cc:17004))
            // return c2usb::result::not_connected;
            return c2usb::result::NO_CONNECTION;
        }
        return session_->send_report(&report);
    }
    std::optional<resolution_multiplier_report> get_multiplier_report() const
    {
        if (!session_)
        {
            return {};
        }
        return session_->multiplier_report();
    }

    hid::session& start([[maybe_unused]] const hid::session::params& params) override
    {
        return session_.emplace(params);
    }
    void stop([[maybe_unused]] hid::session& sess) override { session_.reset(); }

  private:
    std::optional<session> session_{};
};
