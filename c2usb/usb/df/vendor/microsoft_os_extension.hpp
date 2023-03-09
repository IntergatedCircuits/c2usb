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
#ifndef __USB_DF_MICROSOFT_OS_EXTENSION_HPP_
#define __USB_DF_MICROSOFT_OS_EXTENSION_HPP_

#include "usb/df/device.hpp"
#include "usb/vendor/microsoft_os.hpp"

namespace usb::df::microsoft
{
    /// @brief This device extension implements Microsoft OS 2.0 descriptors provision,
    ///        specifically the Compatible ID string for USB functions
    ///        which otherwise don't get correct  driver assigned by Windows.
    class descriptors : public device::extension
    {
    public:
        static descriptors& instance()
        {
            static descriptors d;
            return d;
        }

    protected:
        void control_setup_request(device& dev, message& msg) override;
        unsigned bos_capabilities(device& dev, df::buffer& buffer) override;

        static void get_msos2_descriptor(device& dev, df::buffer& buffer);
        static void get_msos2_config_subset(const config::view& config, uint8_t config_index,
            df::buffer& buffer);
        static void get_msos2_function_subset(const config::interface& iface, uint8_t iface_index,
            df::buffer& buffer);
        static usb::microsoft::platform_descriptor* get_platform_descriptor(device& dev,
            df::buffer& buffer);

        constexpr descriptors()
                : device::extension()
        {}
    };

    /// @brief Non-template base class for @ref alternate_enumeration.
    class alternate_enumeration_base : public descriptors
    {
    public:
        bool alternate_enumerated() const { return using_alt_enum_; }

    protected:
        constexpr alternate_enumeration_base(usb::speeds speeds, uint8_t max_configs_count)
                : descriptors(), speeds_(speeds), max_config_count_(max_configs_count)
        {}
        void bus_reset(device& dev) override { using_alt_enum_ = false; }

        void assign_istrings(device& dev, istring* index) override;
        bool send_owned_string(device& dev, istring index, string_message& smsg) override;
        void control_setup_request(device& dev, message& msg) override;
        unsigned bos_capabilities(device& dev, df::buffer& buffer) override;

        constexpr usb::speeds speeds() const { return speeds_; }
        virtual config::view_list alt_configs_by_speed(usb::speed speed) = 0;

        const usb::speeds speeds_;
        const uint8_t max_config_count_ {};
        bool using_alt_enum_ {};
    };

    /// @brief This device extension implements Microsoft OS 2.0 alternate enumeration,
    ///        meaning that different configurations will be presented to a Windows host than to others.
    template<usb::speeds SPEEDS, size_t MAX_CONFIG_LIST_SIZE = 1>
    class alternate_enumeration : public alternate_enumeration_base
    {
        static_assert(not SPEEDS.includes(speed::NONE));
        static_assert(SPEEDS.min <= SPEEDS.max);
        static_assert(MAX_CONFIG_LIST_SIZE > 0);

        static constexpr bool single_config() { return MAX_CONFIG_LIST_SIZE == 1; }

    public:
        alternate_enumeration()
                : alternate_enumeration_base(SPEEDS, MAX_CONFIG_LIST_SIZE)
        {}

        void set_configs_for_speed(config::view_list configs, usb::speed speed)
        {
            assert(SPEEDS.includes(speed));
            configs_store_[SPEEDS.offset(speed)] = configs;
        }

        void set_configs(const config::view_list& configs)
            requires(SPEEDS.count() == 1)
        {
            return set_configs_for_speed(configs, SPEEDS.min);
        }

        void set_config_for_speed(config::view config, usb::speed speed)
            requires(single_config())
        {
            assert(SPEEDS.includes(speed));
            config_list_store_[SPEEDS.offset(speed)][0] = config;
            return set_configs_for_speed(config_list_store_[SPEEDS.offset(speed)], speed);
        }

        void set_config(config::view config)
            requires(single_config() and (SPEEDS.count() == 1))
        {
            return set_config_for_speed(config, SPEEDS.min);
        }

    private:
        config::view_list configs_by_speed(device& dev, usb::speed speed) override
        {
            assert(SPEEDS.includes(speed));
            if (alternate_enumerated())
            {
                return alt_configs_by_speed(speed);
            }
            else
            {
                return {};
            }
        }
        config::view_list alt_configs_by_speed(usb::speed speed) override
        {
            assert(SPEEDS.includes(speed));
            return configs_store_[SPEEDS.offset(speed)];
        }

        std::array<config::view_list, SPEEDS.count()> configs_store_ {};
        using conditional_store_t = std::conditional_t<single_config(),
                std::array<std::array<config::view, 2>, SPEEDS.count()>, std::monostate>;
        [[no_unique_address]] conditional_store_t config_list_store_ {};
    };
}

#endif // __USB_DF_MICROSOFT_OS_EXTENSION_HPP_
