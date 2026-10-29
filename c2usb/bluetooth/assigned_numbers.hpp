#pragma once
#include "c2usb.hpp"

namespace bluetooth::core
{

// https://bitbucket.org/bluetooth-SIG/public/src/main/assigned_numbers/core/namespaces.yaml
enum struct name_space : uint8_t
{
    unknown = 0,
    bluetooth_sig = 1
};

// https://bitbucket.org/bluetooth-SIG/public/src/main/assigned_numbers/core/namespace.yaml
struct instance
{
    enum struct named : uint16_t
    {
        unknown = 0,
        front = 0x0101,
        back = 0x0102,
        top = 0x0103,
        bottom = 0x0104,
        upper = 0x0105,
        lower = 0x0106,
        main = 0x0107,
        backup = 0x0108,
        auxiliary = 0x0109,
        supplementary = 0x010A,
        flash = 0x010B,
        inside = 0x010C,
        outside = 0x010D,
        left = 0x010E,
        right = 0x010F,
        internal = 0x0110,
        external = 0x0111
    };
    using enum named;

    constexpr instance(named val)
        : value(static_cast<uint16_t>(val))
    {}

    // all inputs except 0 are valid (0 means unknown)
    static constexpr instance numeric(uint8_t val) { return {static_cast<named>(val)}; }

    uint16_t value;
};

// https://bitbucket.org/bluetooth-SIG/public/src/main/assigned_numbers/core/formattypes.yaml
enum struct format : uint8_t
{
    unknown = 0,
    boolean = 0x01,
    uint2 = 0x02,
    uint4 = 0x03,
    uint8 = 0x04,
    uint12 = 0x05,
    uint16 = 0x06,
    uint24 = 0x07,
    uint32 = 0x08,
    uint48 = 0x09,
    uint64 = 0x0A,
    uint128 = 0x0B,
    sint8 = 0x0C,
    sint12 = 0x0D,
    sint16 = 0x0E,
    sint24 = 0x0F,
    sint32 = 0x10,
    sint48 = 0x11,
    sint64 = 0x12,
    sint128 = 0x13,
    float32 = 0x14,
    float64 = 0x15,
    medfloat16 = 0x16,
    medfloat32 = 0x17,
    uint16_2 = 0x18,
    utf8s = 0x19,
    utf16s = 0x1A,
    struct_ = 0x1B,
    medASN1 = 0x1C
};

// https://bitbucket.org/bluetooth-SIG/public/src/main/assigned_numbers/uuids/units.yaml
enum struct unit : uint16_t
{
    unitless = 0x2700,
    length_metre = 0x2701,
    mass_kilogram = 0x2702,
    time_second = 0x2703,
    electric_current_ampere = 0x2704,
    thermodynamic_temperature_kelvin = 0x2705,
    amount_of_substance_mole = 0x2706,
    luminous_intensity_candela = 0x2707,
    area_square_metres = 0x2710,
    volume_cubic_metres = 0x2711,
    velocity_metres_per_second = 0x2712,
    acceleration_metres_per_second_squared = 0x2713,
    wavenumber_reciprocal_metre = 0x2714,
    density_kilogram_per_cubic_metre = 0x2715,
    surface_density_kilogram_per_square_metre = 0x2716,
    specific_volume_cubic_metre_per_kilogram = 0x2717,
    current_density_ampere_per_square_metre = 0x2718,
    magnetic_field_strength_ampere_per_metre = 0x2719,
    amount_concentration_mole_per_cubic_metre = 0x271A,
    mass_concentration_kilogram_per_cubic_metre = 0x271B,
    luminance_candela_per_square_metre = 0x271C,
    refractive_index = 0x271D,
    relative_permeability = 0x271E,
    plane_angle_radian = 0x2720,
    solid_angle_steradian = 0x2721,
    frequency_hertz = 0x2722,
    force_newton = 0x2723,
    pressure_pascal = 0x2724,
    energy_joule = 0x2725,
    power_watt = 0x2726,
    electric_charge_coulomb = 0x2727,
    electric_potential_difference_volt = 0x2728,
    capacitance_farad = 0x2729,
    electric_resistance_ohm = 0x272A,
    electric_conductance_siemens = 0x272B,
    magnetic_flux_weber = 0x272C,
    magnetic_flux_density_tesla = 0x272D,
    inductance_henry = 0x272E,
    celsius_temperature_degree_celsius = 0x272F,
    luminous_flux_lumen = 0x2730,
    illuminance_lux = 0x2731,
    activity_referred_to_a_radionuclide_becquerel = 0x2732,
    absorbed_dose_gray = 0x2733,
    equivalent_dose_sievert = 0x2734,
    catalytic_activity_katal = 0x2735,
    dynamic_viscosity_pascal_second = 0x2740,
    moment_of_force_newton_metre = 0x2741,
    surface_tension_newton_per_metre = 0x2742,
    angular_velocity_radian_per_second = 0x2743,
    angular_acceleration_radian_per_second_squared = 0x2744,
    heat_flux_density_watt_per_square_metre = 0x2745,
    heat_capacity_joule_per_kelvin = 0x2746,
    specific_heat_capacity_joule_per_kilogram_kelvin = 0x2747,
    specific_energy_joule_per_kilogram = 0x2748,
    thermal_conductivity_watt_per_metre_kelvin = 0x2749,
    energy_density_joule_per_cubic_metre = 0x274A,
    electric_field_strength_volt_per_metre = 0x274B,
    electric_charge_density_coulomb_per_cubic_metre = 0x274C,
    surface_charge_density_coulomb_per_square_metre = 0x274D,
    electric_flux_density_coulomb_per_square_metre = 0x274E,
    permittivity_farad_per_metre = 0x274F,
    permeability_henry_per_metre = 0x2750,
    molar_energy_joule_per_mole = 0x2751,
    molar_entropy_joule_per_mole_kelvin = 0x2752,
    exposure_coulomb_per_kilogram = 0x2753,
    absorbed_dose_rate_gray_per_second = 0x2754,
    radiant_intensity_watt_per_steradian = 0x2755,
    radiance_watt_per_square_metre_steradian = 0x2756,
    catalytic_activity_concentration_katal_per_cubic_metre = 0x2757,
    time_minute = 0x2760,
    time_hour = 0x2761,
    time_day = 0x2762,
    plane_angle_degree = 0x2763,
    plane_angle_minute = 0x2764,
    plane_angle_second = 0x2765,
    area_hectare = 0x2766,
    volume_litre = 0x2767,
    mass_tonne = 0x2768,
    pressure_bar = 0x2780,
    pressure_millimetre_of_mercury = 0x2781,
    length_angstrom = 0x2782,
    length_nautical_mile = 0x2783,
    area_barn = 0x2784,
    velocity_knot = 0x2785,
    logarithmic_radio_quantity_neper = 0x2786,
    logarithmic_radio_quantity_bel = 0x2787,
    length_yard = 0x27A0,
    length_parsec = 0x27A1,
    length_inch = 0x27A2,
    length_foot = 0x27A3,
    length_mile = 0x27A4,
    pressure_pound_force_per_square_inch = 0x27A5,
    velocity_kilometre_per_hour = 0x27A6,
    velocity_mile_per_hour = 0x27A7,
    angular_velocity_revolution_per_minute = 0x27A8,
    energy_gram_calorie = 0x27A9,
    energy_kilogram_calorie = 0x27AA,
    energy_kilowatt_hour = 0x27AB,
    thermodynamic_temperature_degree_fahrenheit = 0x27AC,
    percentage = 0x27AD,
    per_mille = 0x27AE,
    period_beats_per_minute = 0x27AF,
    electric_charge_ampere_hours = 0x27B0,
    mass_density_milligram_per_decilitre = 0x27B1,
    mass_density_millimole_per_litre = 0x27B2,
    time_year = 0x27B3,
    time_month = 0x27B4,
    concentration_count_per_cubic_metre = 0x27B5,
    irradiance_watt_per_square_metre = 0x27B6,
    transfer_rate_milliliter_per_kilogram_per_minute = 0x27B7,
    mass_pound = 0x27B8,
    metabolic_equivalent = 0x27B9,
    step_per_minute = 0x27BA,
    stroke_per_minute = 0x27BC,
    pace_kilometre_per_minute = 0x27BD,
    luminous_efficacy_lumen_per_watt = 0x27BE,
    luminous_energy_lumen_hour = 0x27BF,
    luminous_exposure_lux_hour = 0x27C0,
    mass_flow_gram_per_second = 0x27C1,
    volume_flow_litre_per_second = 0x27C2,
    sound_pressure_decibel = 0x27C3,
    parts_per_million = 0x27C4,
    parts_per_billion = 0x27C5,
    mass_density_rate_milligram_per_decilitre_per_minute = 0x27C6,
    electrical_apparent_energy_kilovolt_ampere_hour = 0x27C7,
    electrical_apparent_power_volt_ampere = 0x27C8,
    gravity_gn = 0x27C9,
};

} // namespace bluetooth::core
