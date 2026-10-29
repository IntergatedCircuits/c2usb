#include <cerrno>
#include <cstdint>

#include <zephyr/ztest.h>

#include <bluetooth/battery.hpp>

namespace
{
using namespace bluetooth::battery;
using namespace magic_enum::bitwise_operators;

using basic_service = service_instance<>;
using empty_bls_service = service_instance<option_flags::BATTERY_LEVEL_STATUS>;
using critical_only_service = service_instance<option_flags::BATTERY_CRITICAL_STATUS>;
using bls_level_service =
    service_instance<option_flags::BATTERY_LEVEL_STATUS | option_flags::BLS_BATTERY_LEVEL>;
using bls_additional_service =
    service_instance<option_flags::BATTERY_LEVEL_STATUS | option_flags::BLS_ADDITIONAL_STATUS>;
constexpr auto ALL_FEATURES = option_flags::BATTERY_LEVEL_STATUS | option_flags::BLS_BATTERY_LEVEL |
                              option_flags::BLS_ADDITIONAL_STATUS |
                              option_flags::BATTERY_CRITICAL_STATUS;
using battery_status_service = service_instance<ALL_FEATURES, bluetooth::core::instance::main>;
using all_features_service = service_instance<ALL_FEATURES>;
using described_service = service_instance<option_flags::BATTERY_LEVEL_STATUS,
                                           bluetooth::core::instance::main, "primary">;

void indication_result(::bt_conn*, bluetooth::gatt::attribute::indication&, bluetooth::att::error)
{}
void indication_destroy(bluetooth::gatt::attribute::indication&) {}

static_assert(sizeof(battery_level_status::power_state) == 2);
static_assert(sizeof(battery_critical_status) == 1);
static_assert(sizeof(empty_bls_service::bls_type) == 3);
static_assert(sizeof(bls_level_service::bls_type) == 4);
static_assert(sizeof(bls_additional_service::bls_type) == 4);
static_assert(sizeof(battery_status_service::bls_type) == 7);
static_assert(sizeof(all_features_service::bls_type) == 5);

} // namespace

ZTEST_SUITE(battery, NULL, NULL, NULL, NULL, NULL);

ZTEST(battery, test_feature_and_identifier_layout)
{
    basic_service basic{};
    empty_bls_service empty_bls{};
    critical_only_service critical_only{};
    bls_level_service bls_level{};
    bls_additional_service bls_additional{};
    all_features_service without_identifier{};
    battery_status_service with_identifier{};
    described_service described{};

    zassert_equal(basic.static_service().attr_count, 5);
    zassert_equal(empty_bls.static_service().attr_count, 8);
    zassert_equal(critical_only.static_service().attr_count, 8);
    zassert_equal(bls_level.static_service().attr_count, 8);
    zassert_equal(bls_additional.static_service().attr_count, 8);
    zassert_equal(without_identifier.static_service().attr_count, 11);
    zassert_equal(with_identifier.static_service().attr_count, 11);
    zassert_equal(described.static_service().attr_count, 9);

    const auto* empty_bls_value =
        static_cast<const uint8_t*>(empty_bls.static_service().attrs[6].user_data);
    const auto* bls_level_value =
        static_cast<const uint8_t*>(bls_level.static_service().attrs[6].user_data);
    const auto* bls_additional_value =
        static_cast<const uint8_t*>(bls_additional.static_service().attrs[6].user_data);
    zassert_equal(empty_bls_value[0], 0x00);
    zassert_equal(bls_level_value[0], 0x02);
    zassert_equal(bls_level_value[3], 100);
    zassert_equal(bls_additional_value[0], 0x04);
    zassert_equal(bls_additional_value[3], 0x00);

    const auto* attrs = with_identifier.static_service().attrs;
    const auto* cpf =
        static_cast<const bluetooth::gatt::char_presentation_format*>(attrs[4].user_data);
    const auto* cpf_data = cpf->data();
    const auto* bls = static_cast<const uint8_t*>(attrs[6].user_data);

    zassert_equal(bls[0], 0x07);
    zassert_equal(cpf_data[5], 0x07);
    zassert_equal(cpf_data[6], 0x01);
    zassert_equal(bls[3], cpf_data[5]);
    zassert_equal(bls[4], cpf_data[6]);

    const auto* critical_only_value =
        static_cast<const uint8_t*>(critical_only.static_service().attrs[6].user_data);
    const auto* critical_value = static_cast<const uint8_t*>(attrs[9].user_data);
    zassert_equal(critical_only_value[0], 0x00);
    zassert_equal(critical_value[0], 0x00);
}

ZTEST(battery, test_setters_update_value_attributes)
{
    battery_status_service service{};
    auto gatt_service = service.dynamic_service();
    zassert_true(gatt_service.start());

    const auto* attrs = service.static_service().attrs;
    auto* battery_level = static_cast<uint8_t*>(attrs[2].user_data);
    auto* bls = static_cast<uint8_t*>(attrs[6].user_data);

    zassert_equal(*battery_level, 100);
    zassert_equal(bls[5], 100);
    auto battery_level_result = service.set_battery_level(67);
    zassert_equal(battery_level_result.to_int(), -EAGAIN);
    zassert_equal(*battery_level, 67);
    zassert_equal(bls[5], 67);

    battery_level_status::power_state power_state{};
    power_state = 0x1234;
    zassert_equal(service.set_power_state(power_state).to_int(), -EAGAIN);
    zassert_equal(bls[1], 0x34);
    zassert_equal(bls[2], 0x12);

    battery_level_status::additional_status additional_status{};
    additional_status = 0x05;
    zassert_equal(service.set_additional_status(additional_status).to_int(), -EAGAIN);
    zassert_equal(bls[6], 0x05);

    auto* critical_status_value = static_cast<uint8_t*>(attrs[9].user_data);
    bluetooth::gatt::attribute::indication indication{indication_result, indication_destroy};
    battery_critical_status critical_status{};
    critical_status.critical_power_state = true;
    critical_status.immediate_service_required = true;
    zassert_equal(service.set_critical_status(critical_status, indication).to_int(), -EAGAIN);
    zassert_equal(critical_status_value[0], 0x03);
    zassert_equal(service.set_critical_status(critical_status, indication).to_int(), -ECANCELED);
}
