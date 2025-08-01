// SPDX-License-Identifier: Apache-2.0
#ifndef __HIGH_RESOLUTION_MOUSE_HPP_
#define __HIGH_RESOLUTION_MOUSE_HPP_

#include "hid/application.hpp"
#include <etl/delegate.h>
#include <hid/app/mouse.hpp>
#include <hid/report_protocol.hpp>

// Linux only works if a report ID is used, see https://bugzilla.kernel.org/show_bug.cgi?id=220144
template <std::uint8_t MOUSE_REPORT_ID = 1, std::int16_t AXIS_LIMIT = 127,
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
        std::conditional_t<(AXIS_LIMIT > std::numeric_limits<int8_t>::max()), hid::le_int16_t,
                           int8_t>
            x{};
        std::conditional_t<(AXIS_LIMIT > std::numeric_limits<int8_t>::max()), hid::le_int16_t,
                           int8_t>
            y{};
        std::conditional_t<(WHEEL_LIMIT > std::numeric_limits<int8_t>::max()), hid::le_int16_t,
                           int8_t>
            wheel_y{};
        std::conditional_t<(WHEEL_LIMIT > std::numeric_limits<int8_t>::max()), hid::le_int16_t,
                           int8_t>
            wheel_x{};

        constexpr mouse_report_base() = default;

        bool operator==(const mouse_report_base& other) const = default;
        bool operator!=(const mouse_report_base& other) const = default;

        bool steady() const { return (x == 0) && (y == 0) && (wheel_y == 0) && (wheel_x == 0); }
    };
    using mouse_report = mouse_report_base<MOUSE_REPORT_ID>;
    using resolution_multiplier_report =
        hid::app::mouse::resolution_multiplier_report<MAX_SCROLL_RESOLUTION, MOUSE_REPORT_ID>;
    using resolution_callback = etl::delegate<void(const resolution_multiplier_report&)>;

  private:
    alignas(std::uintptr_t) mouse_report in_report_{};
    alignas(std::uintptr_t) resolution_multiplier_report multiplier_report_{};
    resolution_callback cbk_;
    hid::protocol prot_{};

  public:
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
    static const hid::report_protocol& report_prot()
    {
        static constexpr const auto rd{report_desc()};
        static constexpr const hid::report_protocol rp{rd};
        return rp;
    }

    high_resolution_mouse(resolution_callback cbk)
        : hid::application(report_prot()), cbk_(cbk)
    {}
    auto send(const mouse_report& report)
    {
        in_report_ = report;
        return send_report(&in_report_);
    }
    void start(hid::protocol prot) override
    {
        prot_ = prot;
        receive_report(&multiplier_report_);
    }
    void stop() override
    {
        multiplier_report_ = {};
        cbk_(multiplier_report_);
    }
    void set_report(hid::report::type type, const std::span<const uint8_t>& data) override
    {
        if (type != resolution_multiplier_report::type())
        {
            return;
        }
        if (data.size() == sizeof(resolution_multiplier_report))
        {
            multiplier_report_ =
                *reinterpret_cast<const resolution_multiplier_report*>(data.data());
        }
        else
        {
            multiplier_report_.resolutions = 0;
        }
        cbk_(multiplier_report_);
        receive_report(&multiplier_report_);
    }
    void get_report(hid::report::selector select,
                    [[maybe_unused]] const std::span<uint8_t>& buffer) override
    {
        if (select == mouse_report::selector())
        {
            send_report(&in_report_);
            return;
        }
        if (select == resolution_multiplier_report::selector())
        {
            send_report(&multiplier_report_);
            return;
        }
        // assert(false);
    }
    // void in_report_sent(const std::span<const uint8_t>& data) override {}
    hid::protocol get_protocol() const override { return prot_; }
    const auto& multiplier_report() const { return multiplier_report_; }
};

#endif // __HIGH_RESOLUTION_MOUSE_HPP_
