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
#ifndef __USB_DF_DEVICE_HPP_
#define __USB_DF_DEVICE_HPP_

#include <variant>
#include <etl/delegate.h>
#include <magic_enum.hpp>

#include "usb/df/mac.hpp"
#include "usb/product_info.hpp"

namespace usb::df
{
/// @brief  The language_id_provider allows customization of the string descriptors
///         by allowing template specialization depending on char_t type.
/// @tparam T device class uses this template class with char_t type.
template <typename T>
struct language_id_provider
{
    static constexpr size_t SIZE = 1;
    static const descriptor_header& descriptor()
    {
        using namespace standard::descriptor;
        C2USB_USB_TRANSFER_ALIGN(constexpr static language_id<SIZE>,
                                 lang_id_desc){// The default Unicode language ID is en-US
                                               .wLANGID{0x0409}};
        return lang_id_desc;
    }
};

/// @brief  The device is the non-template base class for @ref device_instance.
class device : public polymorphic
{
  public:
    /// @brief  The extension is the interface for vendor specific device extensions.
    class extension
    {
      public:
        static extension& instance()
        {
            static extension e;
            return e;
        }
        virtual ~extension() = default;

        virtual void bus_reset([[maybe_unused]] device& dev) {}
        virtual void assign_istrings([[maybe_unused]] device& dev, [[maybe_unused]] istring* index)
        {}
        virtual bool send_owned_string([[maybe_unused]] device& dev, [[maybe_unused]] istring index,
                                       [[maybe_unused]] string_message& smsg)
        {
            return false;
        }
        virtual config::view_list configs_by_speed([[maybe_unused]] device& dev,
                                                   [[maybe_unused]] usb::speed speed)
        {
            return {};
        }
        virtual void control_setup_request([[maybe_unused]] device& dev, message& msg)
        {
            return msg.reject();
        }
        virtual void control_data_status([[maybe_unused]] device& dev, message& msg)
        {
            return msg.confirm();
        }
        virtual unsigned bos_capabilities([[maybe_unused]] device& dev,
                                          [[maybe_unused]] df::buffer& buffer)
        {
            return 0;
        }

      protected:
        constexpr extension() = default;
    };

    bool configured() const { return mac_.configured(); }
    usb::speed bus_speed() const { return mac_.speed(); }

    void set_power_source(usb::power::source src) { mac_.set_power_source(src); }
    auto power_source() const
    {
        return static_cast<usb::power::source>(mac_.std_status().self_powered);
    }
    uint32_t granted_bus_current_uA() const { return mac_.granted_bus_current_uA(); }
    bool allows_remote_wakeup() const { return mac_.std_status().remote_wakeup; }
    auto power_state() const { return mac_.power_state(); }
    const config::power* power_config() const;
    auto remote_wakeup() { return mac_.remote_wakeup(); }

    // using namespace magic_enum::bitwise_operators;
    enum class event : uint8_t
    {
        NONE = 0,
        POWER_STATE_CHANGE = 1,
        CONFIGURATION_CHANGE = 2,
    };
    using power_event_delegate = etl::delegate<void(device&, event)>;
    void set_power_event_delegate(const power_event_delegate& delegate)
    {
        power_event_delegate_ = delegate;
    }

    virtual config::view_list configs_by_speed(usb::speed speed) = 0;

    constexpr version usb_spec_version() { return version("2.0.1"); }
    usb::speeds speeds() const { return speeds_; }

    void open();
    void close();
    bool is_open() const;

  protected:
    virtual void get_descriptor(message& msg);
    void get_descriptor_dual_speed(message& msg);

    template <bool DUAL_SPEED>
    void get_descriptor_by_speed_support(message& msg);

    device(usb::df::mac& mac, const product_info& prodinfo, usb::speeds speeds,
           uint8_t max_configs_count, extension& ext)
        : mac_(mac),
          product_info_(prodinfo),
          extension_(ext),
          speeds_(speeds),
          max_config_count_(max_configs_count),
          istr_config_base_(ISTR_GLOBAL_BASE - max_configs_count * speeds.count())
    {
        mac_.init(*this, speeds);
    }
    ~device() override { mac_.deinit(*this); }

    void assign_function_istrings();
    void get_device_qualifier_descriptor(message& msg, usb::speed speed);
    void get_config_descriptor(message& msg, usb::speed speed);

    uint8_t max_config_count() const { return max_config_count_; }

  private:
    void get_function_string(istring index, string_message& smsg);
    void get_config_string(istring index, string_message& smsg);
    istring get_config_istring(uint8_t config_index, usb::speed speed);
    istring istr_config_base() const { return istr_config_base_; }

    void interface_control(message& msg,
                           void (function::*handler)(message&, const config::interface&));

    void get_string_descriptor(message& msg);
    void get_device_descriptor(message& msg);
    void get_bos_descriptor(message& msg);
    void set_address(message& msg);
    void set_config(config::view config, event ev = event::CONFIGURATION_CHANGE);
    void set_configuration(message& msg);
    void get_configuration(message& msg);
    void get_status(message& msg);
    void set_feature(message& msg, bool active);

    void device_setup_request(message& msg);
    void endpoint_setup_request(message& msg);

    void delegate_power_event(event ev);

    friend class mac;

    void on_power_state_change(power::state new_state);
    void on_bus_reset(event ev);
    void on_control_setup(message& msg);
    void on_control_data(message& msg);

  protected:
    usb::df::mac& mac_;
    const product_info& product_info_;
    extension& extension_;
    power_event_delegate power_event_delegate_{};

  private:
    static constexpr istring ISTR_VENDOR_NAME = 0xFF;
    static constexpr istring ISTR_PRODUCT_NAME = 0xFE;
    static constexpr istring ISTR_SERIAL_NUMBER = 0xFD;
    static constexpr istring ISTR_GLOBAL_BASE = ISTR_SERIAL_NUMBER;

    const usb::speeds speeds_;
    const uint8_t max_config_count_;
    const uint8_t istr_config_base_;
};

/// @brief  The device_instance class is the high level controller of the USB link on the device
/// side.
///         It manages the configurations and functions, and handles the device-related control
///         messages.
///
/// @tparam SPEEDS: Select which bus speeds may have an active configuration associated with it.
///         If only a single speed is supported, @ref set_configs API is available.
///
/// @tparam MAX_CONFIG_LIST_SIZE: The maximum allowed size of a config_list that is passed
///         through the set_configs and set_configs_for_speed methods.
///         When set to 1, local buffer is allocated so the @ref set_config_for_speed()
///         and @ref set_config() simplified APIs are available.
///         Most USB devices have a single configuration (per speed), therefore it defaults to 1.
///
template <usb::speeds SPEEDS, size_t MAX_CONFIG_LIST_SIZE = 1>
class device_instance : public device
{
    static_assert(not SPEEDS.includes(speed::NONE) and (SPEEDS.min <= SPEEDS.max));
    static_assert(MAX_CONFIG_LIST_SIZE > 0);
    static constexpr bool single_config() { return MAX_CONFIG_LIST_SIZE == 1; }

  public:
    device_instance(usb::df::mac& mac, const product_info& prodinfo,
                    extension& ext = extension::instance())
        : device(mac, prodinfo, SPEEDS, MAX_CONFIG_LIST_SIZE, ext)
    {}

    /// @brief  Sets the configuration list for a given bus speed. The device performs soft-detach
    ///         to force re-enumeration, so the host can interrogate the new configuration list.
    /// @param  configs: configuration list to use
    /// @param  speed: bus speed for which the configuration list is intended
    void set_configs_for_speed(const config::view_list& configs, usb::speed speed)
    {
        assert(SPEEDS.includes(speed));
        assert(not is_open());
        configs_store_[SPEEDS.offset(speed)] = configs;
        assign_function_istrings();
    }

    /// @brief  Simplification of @ref set_configs_for_speed() when only one speed is supported.
    /// @param  configs: configuration list to use
    void set_configs(const config::view_list& configs)
        requires((SPEEDS.count() == 1))
    {
        return set_configs_for_speed(configs, SPEEDS.min);
    }

    /// @brief  Simplification of @ref set_configs_for_speed() when single configuration storage is
    /// enabled.
    /// @param  config: single configuration to use
    /// @param  speed: bus speed for which the configuration is intended
    void set_config_for_speed(config::view config, usb::speed speed)
        requires(single_config())
    {
        assert(SPEEDS.includes(speed));
        config_list_store_[SPEEDS.offset(speed)][0] = config;
        return set_configs_for_speed(config_list_store_[SPEEDS.offset(speed)], speed);
    }

    /// @brief  Simplification of @ref set_configs_for_speed() when only one speed is supported and
    ///         single configuration storage is enabled.
    /// @param  config: single configuration to use
    void set_config(config::view config)
        requires(single_config() and (SPEEDS.count() == 1))
    {
        return set_config_for_speed(config, SPEEDS.min);
    }

    config::view_list configs_by_speed(usb::speed speed) override
    {
        auto configs = extension_.configs_by_speed(*this, speed);
        if (not configs.empty())
        {
            return configs;
        }
        return configs_store_[SPEEDS.offset(speed)];
    }

  private:
    /// @brief Provides descriptors necessary for high-speed operation, on top of the standards
    /// ones.
    /// @param msg: the control message to reply to
    void get_descriptor(message& msg) override
    {
        return get_descriptor_by_speed_support<(
            SPEEDS.includes(usb::speeds(speed::FULL, speed::HIGH)))>(msg);
    }

    std::array<config::view_list, SPEEDS.count()> configs_store_{};
    using conditional_store_t =
        std::conditional_t<single_config(), std::array<std::array<config::view, 2>, SPEEDS.count()>,
                           std::monostate>;
    [[no_unique_address]] conditional_store_t config_list_store_{};
};
} // namespace usb::df

template <>
struct magic_enum::customize::enum_range<usb::df::device::event>
{
    static constexpr bool is_flags = true;
};

#endif // __USB_DF_DEVICE_HPP_
