// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cassert>
#include "usb/df/vendor/extension.hpp"
#include "usb/vendor/webusb.hpp"

namespace usb::df
{
class webusb : public usb::df::vendor::extension
{
  public:
    constexpr explicit webusb(const char8_t* url,
                              usb::webusb::url_scheme scheme = usb::webusb::url_scheme::HTTPS,
                              const char_t* landing_page = nullptr)
        : url_(url), landing_page_(landing_page), scheme_(scheme)
    {}

  private:
    bool control_setup_request([[maybe_unused]] device& dev, message& msg) override
    {
        using namespace usb::webusb::control;

        if (msg.request() == WEBUSB)
        {
            switch (usb::webusb::request_code(uint16_t(msg.request().wIndex)))
            {
            case usb::webusb::request_code::GET_URL:
            {
                std::u8string_view url_view(url_);
                assert(usb::webusb::url_descriptor::url_offset() + url_view.size() <=
                       msg.buffer().max_size());
                auto* url_desc =
                    msg.buffer().allocate<usb::webusb::url_descriptor>(url_view.size());
                url_desc->bScheme = scheme_;
                std::ignore = msg.buffer().allocate(url_view.size());
                std::copy(url_view.begin(), url_view.end(), url_desc->URL);
                msg.send_buffer();
                return true;
            }

            default:
                return false;
            }
        }
        return false;
    }
    [[nodiscard]] unsigned bos_capabilities([[maybe_unused]] const device& dev,
                                            df::buffer& buffer) const override
    {
        auto* platform_desc = buffer.allocate<usb::webusb::platform_descriptor>();
        platform_desc->CapabilityData.iLandingPage = landing_page_index_;
        return 1;
    }
    void assign_istrings([[maybe_unused]] device& dev, [[maybe_unused]] istring* index) override
    {
        if (landing_page_)
        {
            landing_page_index_ = *index;
            ++(*index);
        }
    }
    bool send_owned_string([[maybe_unused]] const device& dev, istring index,
                           string_message& smsg) const override
    {
        if (index == landing_page_index_)
        {
            smsg.send_string(landing_page_);
            return true;
        }
        return false;
    }

    const char8_t* url_;
    const char_t* landing_page_;
    usb::webusb::url_scheme scheme_;
    istring landing_page_index_{};
};

} // namespace usb::df
