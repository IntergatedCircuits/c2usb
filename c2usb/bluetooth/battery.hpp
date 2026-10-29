// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <bit>
#include <memory>
#include <span>
#include "bluetooth/gatt.hpp"
#include "ct_string.hpp"
#include <bitfilled.hpp>
#include <type_traits>

namespace bluetooth::battery
{
enum struct option_flags : uint32_t
{
    NONE = 0,
    BATTERY_LEVEL_STATUS = 1u << 0,
    BLS_BATTERY_LEVEL = 1u << 1,
    BLS_ADDITIONAL_STATUS = 1u << 2,
    BATTERY_CRITICAL_STATUS = 1u << 3,
};
} // namespace bluetooth::battery

template <>
struct magic_enum::customize::enum_range<bluetooth::battery::option_flags>
{
    static constexpr bool is_flags = true;
};

namespace bluetooth::battery
{
template <typename T, bool = std::is_class_v<T>>
struct optional_value_storage : T
{
    using T::operator=;
};
template <typename T>
struct optional_value_storage<T, false> : bitfilled::host_integer<T>
{
    using bitfilled::host_integer<T>::operator=;
};
template <bool Enabled, typename T, size_t ID = __COUNTER__>
struct optional_value
{};
template <typename T, size_t ID>
struct optional_value<true, T, ID> : optional_value_storage<T>
{
    using optional_value_storage<T>::operator=;
};

class battery_level_status
{
  public:
    enum struct flags : uint8_t
    {
        NONE = 0,
        IDENTIFIER_PRESENT = 1 << 0,
        BATTERY_LEVEL_PRESENT = 1 << 1,
        ADDITIONAL_STATUS_PRESENT = 1 << 2,
    };
    enum struct optional_boolean : uint8_t
    {
        NO = 0,
        YES = 1,
        UNKNOWN = 2
    };
    enum struct battery_charge_state : uint8_t
    {
        UNKNOWN = 0,
        CHARGING = 1,
        DISCHARGING_ACTIVE = 2,
        DISCHARGING_INACTIVE = 3
    };
    enum struct battery_charge_level : uint8_t
    {
        UNKNOWN = 0,
        GOOD = 1,
        LOW = 2,
        CRITICAL = 3
    };
    enum struct battery_charging_type : uint8_t
    {
        UNKNOWN = 0,
        CONSTANT_CURRENT = 1,
        CONSTANT_VOLTAGE = 2,
        TRICKLE = 3,
        FLOAT = 4
    };
    enum struct charging_fault_reason : uint8_t
    {
        NONE = 0,
        BATTERY = 1 << 0,
        EXTERNAL_POWER_SOURCE = 1 << 1,
        OTHER = 1 << 2
    };
    struct power_state : c2usb::le_uint16_t
    {
        BF_COPY_SUPERCLASS(power_state)
        BF_BITS(bool, 0) battery_present;
        BF_BITS(optional_boolean, 1, 2) wired_ext_power_src_conn;
        BF_BITS(optional_boolean, 3, 4) wireless_ext_power_src_conn;
        BF_BITS(battery_charge_state, 5, 6) charge_state;
        BF_BITS(battery_charge_level, 7, 8) charge_level;
        BF_BITS(battery_charging_type, 9, 11) charging_type;
        BF_BITS(charging_fault_reason, 12, 14) charging_fault;
    };

    struct additional_status : bitfilled::host_integer<uint8_t>
    {
        BF_COPY_SUPERCLASS(additional_status)
        BF_BITS(optional_boolean, 0, 1) service_required;
        BF_BITS(bool, 2) battery_fault;
    };

    template <battery_level_status::flags Flags>
    struct layout_by
    {
        battery_level_status::flags flags{Flags};
        battery_level_status::power_state power_state;
        [[no_unique_address]] optional_value<
            enum_contains(Flags, battery_level_status::flags::IDENTIFIER_PRESENT), le_uint16_t>
            identifier{};
        [[no_unique_address]] optional_value<
            enum_contains(Flags, battery_level_status::flags::BATTERY_LEVEL_PRESENT), uint8_t>
            battery_level{};
        [[no_unique_address]] optional_value<
            enum_contains(Flags, battery_level_status::flags::ADDITIONAL_STATUS_PRESENT),
            battery_level_status::additional_status> additional_status{};
    };
};

struct battery_critical_status : bitfilled::host_integer<uint8_t>
{
    BF_COPY_SUPERCLASS(battery_critical_status)
    BF_BITS(bool, 0) critical_power_state;
    BF_BITS(bool, 1) immediate_service_required;
};

} // namespace bluetooth::battery

template <>
struct magic_enum::customize::enum_range<bluetooth::battery::battery_level_status::flags>
{
    static constexpr bool is_flags = true;
};

namespace bluetooth::battery
{
class service
{
  protected:
    static const gatt::char_decl& battery_level_info()
    {
        using namespace bluetooth::gatt;
        using namespace magic_enum::bitwise_operators;
        static const gatt::char_decl info{uuid16<BT_UUID_BAS_BATTERY_LEVEL_VAL>(),
                                          properties::READ | properties::NOTIFY};
        return info;
    }
    static const gatt::char_decl& battery_level_status_info()
    {
        using namespace bluetooth::gatt;
        using namespace magic_enum::bitwise_operators;
        static const gatt::char_decl info{uuid16<BT_UUID_BAS_BATTERY_LEVEL_STATUS_VAL>(),
                                          properties::READ | properties::NOTIFY};
        return info;
    }
    static const gatt::char_decl& estimated_service_date_info()
    {
        using namespace bluetooth::gatt;
        static const gatt::char_decl info{uuid16<BT_UUID_GATT_ESD_VAL>(), properties::READ};
        return info;
    }
    static const gatt::char_decl& battery_critical_status_info()
    {
        using namespace bluetooth::gatt;
        using namespace magic_enum::bitwise_operators;
        static const gatt::char_decl info{uuid16<BT_UUID_BAS_BATTERY_CRIT_STATUS_VAL>(),
                                          properties::READ | properties::INDICATE};
        return info;
    }
    static const gatt::char_decl& battery_energy_status_info()
    {
        using namespace bluetooth::gatt;
        using namespace magic_enum::bitwise_operators;
        static const gatt::char_decl info{uuid16<BT_UUID_BAS_BATTERY_ENERGY_STATUS_VAL>(),
                                          properties::READ | properties::NOTIFY |
                                              properties::INDICATE};
        return info;
    }
    static const gatt::char_decl& battery_time_status_info()
    {
        using namespace bluetooth::gatt;
        using namespace magic_enum::bitwise_operators;
        static const gatt::char_decl info{uuid16<BT_UUID_BAS_BATTERY_TIME_STATUS_VAL>(),
                                          properties::READ | properties::NOTIFY |
                                              properties::INDICATE};
        return info;
    }
    static const gatt::char_decl& battery_health_status_info()
    {
        using namespace bluetooth::gatt;
        using namespace magic_enum::bitwise_operators;
        static const gatt::char_decl info{uuid16<BT_UUID_BAS_BATTERY_HEALTH_STATUS_VAL>(),
                                          properties::READ | properties::NOTIFY |
                                              properties::INDICATE};
        return info;
    }
    static const gatt::char_decl& battery_health_information_info()
    {
        using namespace bluetooth::gatt;
        static const gatt::char_decl info{uuid16<BT_UUID_BAS_BATTERY_HEALTH_INF_VAL>(),
                                          properties::READ};
        return info;
    }
    static const gatt::char_decl& battery_information_info()
    {
        using namespace bluetooth::gatt;
        using namespace magic_enum::bitwise_operators;
        static const gatt::char_decl info{uuid16<BT_UUID_BAS_BATTERY_INF_VAL>(),
                                          properties::READ | properties::INDICATE};
        return info;
    }
    static const gatt::char_decl& manufacturer_name_info()
    {
        using namespace bluetooth::gatt;
        static const gatt::char_decl info{uuid16<BT_UUID_DIS_MANUFACTURER_NAME_VAL>(),
                                          properties::READ};
        return info;
    }
    static const gatt::char_decl& model_number_info()
    {
        using namespace bluetooth::gatt;
        static const gatt::char_decl info{uuid16<BT_UUID_DIS_MODEL_NUMBER_VAL>(), properties::READ};
        return info;
    }
    static const gatt::char_decl& serial_number_info()
    {
        using namespace bluetooth::gatt;
        static const gatt::char_decl info{uuid16<BT_UUID_DIS_SERIAL_NUMBER_VAL>(),
                                          properties::READ};
        return info;
    }

    static constexpr size_t base_attribute_count()
    {
        // 1 primary service + 1 characteristic + 1 CCC + 1 CPF
        return 1 + 2 + 1 + 1;
    }
    static gatt::attribute::builder make_base_attributes(security level, gatt::attribute* attr,
                                                         gatt::ccc_store* ccc, uint8_t* battery_lvl,
                                                         const gatt::char_presentation_format* cpf)
    {
        return gatt::attribute::builder(attr)
            .primary_service(uuid16<BT_UUID_BAS_VAL>())
            .characteristic(battery_level_info(), gatt::readable_at(level),
                            &gatt::attribute::read_static_data<sizeof(*battery_lvl)>, nullptr,
                            battery_lvl)
            .ccc_descriptor(*ccc, gatt::readwriteable_at(level))
            .descriptor(uuid16<BT_UUID_GATT_CPF_VAL>(), gatt::readable_at(level),
                        &gatt::attribute::read_static_data<sizeof(*cpf)>, nullptr, cpf);
        ;
    }
};

template <option_flags Features = option_flags::NONE,
          core::instance Identifier = core::instance::unknown,
          ct_string Description = ct_string{""}>
class service_instance : public service
{
    static_assert(not enum_contains(Features, option_flags::BLS_BATTERY_LEVEL) or
                  enum_contains(Features, option_flags::BATTERY_LEVEL_STATUS));
    static_assert(not enum_contains(Features, option_flags::BLS_ADDITIONAL_STATUS) or
                  enum_contains(Features, option_flags::BATTERY_LEVEL_STATUS));

    static constexpr bool has_identifier()
    {
        return Identifier.value != static_cast<uint16_t>(core::instance::unknown);
    }
    static constexpr bool has_description() { return Description.value.size() > 1; }

    static constexpr size_t attribute_count()
    {
        return base_attribute_count() + has_description() +
               enum_contains(Features, option_flags::BATTERY_LEVEL_STATUS) * 3 +
               enum_contains(Features, option_flags::BATTERY_CRITICAL_STATUS) * 3;
    }
    static constexpr size_t ccc_count()
    {
        return 1 + enum_contains(Features, option_flags::BATTERY_LEVEL_STATUS) +
               enum_contains(Features, option_flags::BATTERY_CRITICAL_STATUS);
    }
    static constexpr size_t bls_value_attribute_index()
    {
        return base_attribute_count() + has_description() + 1;
    }
    static constexpr size_t bcs_value_attribute_index()
    {
        return base_attribute_count() + has_description() +
               enum_contains(Features, option_flags::BATTERY_LEVEL_STATUS) * 3 + 1;
    }

    static constexpr battery_level_status::flags bls_flags()
    {
        using namespace magic_enum::bitwise_operators;
        auto flags = battery_level_status::flags::NONE;
        if constexpr (has_identifier())
        {
            flags |= battery_level_status::flags::IDENTIFIER_PRESENT;
        }
        if constexpr (enum_contains(Features, option_flags::BLS_BATTERY_LEVEL))
        {
            flags |= battery_level_status::flags::BATTERY_LEVEL_PRESENT;
        }
        if constexpr (enum_contains(Features, option_flags::BLS_ADDITIONAL_STATUS))
        {
            flags |= battery_level_status::flags::ADDITIONAL_STATUS_PRESENT;
        }
        return flags;
    }

  public:
    using bls_type = battery_level_status::layout_by<bls_flags()>;

    /// @brief  Sets the battery level as a percentage, and sends notification to subscribed clients
    /// @param  level: battery level (0-100)
    /// @return result of the notification operation
    c2usb::result set_battery_level(uint8_t level)
    {
        if (level > 100)
        {
            return std::errc::invalid_argument;
        }
        if (level == battery_lvl_)
        {
            return c2usb::result::ok;
        }
        battery_lvl_ = level;
        if constexpr (enum_contains(Features, option_flags::BLS_BATTERY_LEVEL))
        {
            static_cast<bls_type&>(bls_).battery_level = level;
        }

        auto battery_level_result = notify_value(attributes_[2], battery_lvl_);
        if constexpr (enum_contains(Features, option_flags::BLS_BATTERY_LEVEL))
        {
            auto bls_result = notify_bls();
            if (battery_level_result == c2usb::result::ok)
            {
                battery_level_result = bls_result;
            }
        }
        return battery_level_result;
    }

    /// @brief  Sets the power state and sends notification to subscribed clients
    /// @param  state: new power state
    /// @return result of the notification operation
    c2usb::result set_power_state(battery_level_status::power_state state)
        requires(enum_contains(Features, option_flags::BATTERY_LEVEL_STATUS))
    {
        auto& current = static_cast<bls_type&>(bls_).power_state;
        if (current == state)
        {
            return c2usb::result::ok;
        }
        current = state;
        return notify_bls();
    }

    /// @brief  Sets the additional status and sends notification to subscribed clients
    /// @param  status: new additional status
    /// @return result of the notification operation
    c2usb::result set_additional_status(battery_level_status::additional_status status)
        requires(enum_contains(Features, option_flags::BLS_ADDITIONAL_STATUS))
    {
        auto& current = static_cast<bls_type&>(bls_).additional_status;
        if (current == status)
        {
            return c2usb::result::ok;
        }
        current = status;
        return notify_bls();
    }

    /// @brief  Sets the critical status and sends indication to subscribed clients
    /// @param  status: new critical status
    /// @param  indication: indication object used for returning the indication
    /// @return result of the indication operation
    c2usb::result set_critical_status(battery_critical_status status,
                                      gatt::attribute::indication& indication)
        requires(enum_contains(Features, option_flags::BATTERY_CRITICAL_STATUS))
    {
        auto& current = static_cast<battery_critical_status&>(bcs_);
        if (current == status)
        {
            // return with an error as the indication won't be triggered
            return std::errc::operation_canceled;
        }
        current = status;
        return indicate_value(attributes_[bcs_value_attribute_index()], current, indication);
    }

    /// @brief  Constructs a service instance with the specified security level
    /// @param  level: security level for the service
    constexpr service_instance(bluetooth::security level = bluetooth::security::L1_NONE)
    {
        auto* ccc = ccc_stores_.data();
        auto tail = make_base_attributes(level, attributes_.data(), ccc++, &battery_lvl_,
                                         &battery_level_format());
        if constexpr (has_description())
        {
            tail = tail.descriptor(uuid16<BT_UUID_GATT_CUD_VAL>(), gatt::readable_at(level),
                                   &gatt::attribute::read_string, nullptr, &Description);
        }
        if constexpr (enum_contains(Features, option_flags::BATTERY_LEVEL_STATUS))
        {
            auto& bls_value = static_cast<bls_type&>(bls_);
            if constexpr (enum_contains(Features, option_flags::BLS_BATTERY_LEVEL))
            {
                bls_value.battery_level = battery_lvl_;
            }
            if constexpr (has_identifier())
            {
                bls_value.identifier = Identifier.value;
            }
            tail = tail.characteristic(battery_level_status_info(), gatt::readable_at(level),
                                       &gatt::attribute::read_static_data<sizeof(bls_type)>,
                                       nullptr, &static_cast<bls_type&>(bls_))
                       .ccc_descriptor(*ccc++, gatt::readwriteable_at(level));
        }
        if constexpr (enum_contains(Features, option_flags::BATTERY_CRITICAL_STATUS))
        {
            tail = tail.characteristic(
                           battery_critical_status_info(), gatt::readable_at(level),
                           &gatt::attribute::read_static_data<sizeof(battery_critical_status)>,
                           nullptr, &static_cast<battery_critical_status&>(bcs_))
                       .ccc_descriptor(*ccc++, gatt::readwriteable_at(level));
        }
        assert(tail.data() == (attributes_.data() + attributes_.size()));
        assert(ccc == (ccc_stores_.data() + ccc_stores_.size()));
    }

    // to be assigned as:
    // static const STRUCT_SECTION_ITERABLE(bt_gatt_service_static, bas_svc) =
    [[nodiscard]] constexpr ::bt_gatt_service_static static_service() const
    {
        return ::bt_gatt_service_static{.attrs = attributes_.data(),
                                        .attr_count = attributes_.size()};
    }
    // only use either static_service() or dynamic_service(), not both
#if CONFIG_BT_GATT_DYNAMIC_DB
    // can be enabled or disabled at runtime, but preferably started before bt_enable()
    [[nodiscard]] constexpr auto dynamic_service()
    {
        return bluetooth::gatt::optional_service{std::span<gatt::attribute>{attributes_}};
    }
#endif

  private:
    template <typename T>
    static c2usb::result notify_value(const gatt::attribute& attr, const T& value)
    {
        const auto* data = reinterpret_cast<const uint8_t*>(std::addressof(value));
        auto result = attr.notify(std::span<const uint8_t>{data, sizeof(value)});
        if (result == std::errc::not_connected)
        {
            return c2usb::result::ok;
        }
        return c2usb::result{result};
    }
    template <typename T>
    static c2usb::result indicate_value(const gatt::attribute& attr, const T& value,
                                        gatt::attribute::indication& indication)
    {
        const auto* data = reinterpret_cast<const uint8_t*>(std::addressof(value));
        auto result = attr.indicate(std::span<const uint8_t>{data, sizeof(value)}, indication);
        return c2usb::result{result};
    }

    c2usb::result notify_bls()
        requires(enum_contains(Features, option_flags::BATTERY_LEVEL_STATUS))
    {
        return notify_value(attributes_[bls_value_attribute_index()],
                            static_cast<const bls_type&>(bls_));
    }

    static const gatt::char_presentation_format& battery_level_format()
    {
        static const gatt::char_presentation_format cpf(core::format::uint8, core::unit::percentage,
                                                        Identifier, 0);
        return cpf;
    }

    std::array<gatt::attribute, attribute_count()> attributes_{};
    std::array<gatt::ccc_store, ccc_count()> ccc_stores_{};

    uint8_t battery_lvl_{100};

    template <option_flags Flag, typename T>
    using optional_member = optional_value<enum_contains(Features, Flag), T>;

    [[no_unique_address]] optional_member<option_flags::BATTERY_LEVEL_STATUS, bls_type> bls_{};
    [[no_unique_address]] optional_member<option_flags::BATTERY_CRITICAL_STATUS,
                                          battery_critical_status> bcs_{};
};

} // namespace bluetooth::battery
