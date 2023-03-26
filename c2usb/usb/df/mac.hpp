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
#ifndef __USB_DF_MAC_HPP_
#define __USB_DF_MAC_HPP_

#include "usb/speeds.hpp"
#include "usb/standard/descriptors.hpp"
#include "usb/standard/requests.hpp"
#include "usb/df/message.hpp"
#include "usb/df/config.hpp"

namespace usb::df
{
    /// @brief  The mac class serves as the USB device's Media Access Controller,
    ///         allowing access to bus resources.
    class mac : private message
    {
    public:
        using lpm_support_flags = usb::standard::descriptor::device_capability::usb_2p0_extension::attributes;

        class device_interface
        {
        public:
            virtual void handle_reset_request() = 0;
            virtual void handle_control_message(message&) = 0;
            virtual void handle_new_power_state(power::state) = 0;
            virtual ~device_interface() = default;
            constexpr device_interface() = default;
        };

        virtual usb::speed speed()          const { return speed::FULL; }

        config::view active_config()        const { return active_config_; }
        bool configured()                   const { return active_config().valid(); }

        void set_config(config::view config);


        virtual void control_ep_open() {}

        uint16_t control_ep_max_packet_size(usb::speed speed) const
        {
            return endpoint::packet_size_limit(endpoint::type::CONTROL, speed);
        }
        message* get_pending_message(function* caller = nullptr);

        virtual ep_handle ep_open(const config::endpoint& ep) { return {}; }
        virtual result ep_send(ep_handle eph, const std::span<const uint8_t>& data) { return result::NO_TRANSPORT; }
        virtual result ep_receive(ep_handle eph, const std::span<uint8_t>& data) { return result::NO_TRANSPORT; }
        virtual result ep_close(ep_handle eph) { return result::NO_TRANSPORT; }

        virtual bool ep_is_stalled(ep_handle eph) const { return false; }
        virtual result ep_change_stall(ep_handle eph, bool stall) { return result::NO_TRANSPORT; }


        void init(device_interface& dev_if, const usb::speeds& speeds, const std::span<uint8_t>& buffer);
        void deinit(device_interface& dev_if);
        virtual void soft_attach() {}
        virtual void soft_detach() {}
        void start();
        void stop();
        bool running() const { return running_; }

        standard::device::status std_status() const { return std_status_; }

        // used as bmAttributes in USB 2p0 extension descriptor (LPM)
        virtual lpm_support_flags lpm_support() { return {}; }

        power::state power_state() const { return power_state_; }
        uint32_t granted_bus_current_uA() const;
        bool remote_wakeup();

        void set_remote_wakeup(bool enabled);
        void set_power_source(usb::power::source src);


        virtual const config::endpoint& ep_address_to_config(endpoint::address addr) const;
        virtual ep_handle ep_address_to_handle(endpoint::address addr) const = 0;
        virtual const config::endpoint& ep_handle_to_config(ep_handle eph) const = 0;
        virtual ep_handle ep_config_to_handle(const config::endpoint& ep) const = 0;

    protected:
        control::request& request() { return request_; }
        using message::stage;

        virtual void allocate_endpoints(config::view config = {}) {}

        virtual endpoint::address ep_handle_to_address(ep_handle eph) const = 0;
        //virtual void control_reply(direction dir, const transfer& t) override {};

        void control_ep_setup();
        void control_ep_data(direction ep_dir, const transfer& t);
        void ep_transfer_complete(ep_handle eph, const transfer& t);
        virtual void init(const usb::speeds& speeds) {}
        virtual void deinit() {}
        virtual void signal_remote_wakeup() {}

        static ep_handle create_ep_handle(uint8_t raw) { return ep_handle(raw); }

        void bus_reset();

        void set_power_state(power::state new_state);

        constexpr mac() = default;

    private:
        standard::device::status std_status_ {};
        power::state power_state_ {};
        bool running_ {};
        config::view active_config_ {};
        device_interface* dev_if_ {};
    };

    /// @brief  MAC specialization that uses the config index of endpoints as handles.
    class index_handle_mac : public mac
    {
    public:
        using mac::mac;

        ep_handle ep_address_to_handle(endpoint::address addr) const override;
        const config::endpoint& ep_handle_to_config(ep_handle eph) const override;
    protected:
        endpoint::address ep_handle_to_address(ep_handle eph) const override;
        ep_handle ep_config_to_handle(const config::endpoint& ep) const override;
    };

    /// @brief  MAC specialization that uses the endpoint addresses as handles.
    class address_handle_mac : public mac
    {
    public:
        using mac::mac;

        ep_handle ep_address_to_handle(endpoint::address addr) const override;
        const config::endpoint& ep_handle_to_config(ep_handle eph) const override;
    protected:
        endpoint::address ep_handle_to_address(ep_handle eph) const override
        {
            return endpoint::address(eph);
        }
        ep_handle ep_config_to_handle(const config::endpoint& ep) const override
        {
            return ep_address_to_handle(ep.address());
        }
    };
}

#endif // __USB_DF_MAC_HPP_
