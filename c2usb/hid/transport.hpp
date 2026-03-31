// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cassert>
#include <span>
#include "hid/application.hpp"

namespace hid
{
/// @brief  Interface for different transport layers.
class transport : public c2usb::interface
{
  public:
    /// @brief  Helper class to manage FEATURE and OUTPUT buffers.
    class reports_receiver
    {
      public:
        constexpr reports_receiver() = default;

        constexpr std::span<uint8_t>& operator[](report::type type)
        {
            return buffers_[static_cast<uint8_t>(type) - OFFSET];
        }
        constexpr const std::span<uint8_t>& operator[](report::type type) const
        {
            return buffers_[static_cast<uint8_t>(type) - OFFSET];
        }
        constexpr std::span<uint8_t>& largest()
        {
            return (buffers_[0].size() > buffers_[1].size()) ? buffers_[0] : buffers_[1];
        }

      private:
        constexpr static auto OFFSET =
            static_cast<uint8_t>(std::min(report::type::FEATURE, report::type::OUTPUT));
        static_assert((static_cast<uint8_t>(std::max(report::type::FEATURE, report::type::OUTPUT)) -
                       OFFSET) == 1);

        std::array<std::span<uint8_t>, 2> buffers_{};
    };

    virtual c2usb::result send_report(session& sess, const std::span<const uint8_t>& data) = 0;
    virtual c2usb::result receive_report(session& sess, const std::span<uint8_t>& data,
                                         report::type type = report::type::OUTPUT) = 0;

  protected:
    void start(application& app, session*& sess, const session::params& params)
    {
        assert(params.transport == this);
        if (sess != nullptr)
        {
            if (sess->boot_protocol() == params.boot_protocol)
            {
                return;
            }
            else
            {
                app.stop(*sess);
            }
        }
        sess = &app.start(params);
    }
    void stop(application& app, session*& sess)
    {
        if (sess != nullptr)
        {
            app.stop(*sess);
            sess = nullptr;
        }
    }
};

} // namespace hid
