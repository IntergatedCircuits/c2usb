// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <chrono>
#include <span>
#include <string_view>
#include <type_traits>
#if __has_include("zephyr/bluetooth/bluetooth.h")
#include <zephyr/bluetooth/bluetooth.h>
#else
#error "Unsupported platform"
#endif
#include "bluetooth/uuid.hpp"
#include "c2usb.hpp"

namespace bluetooth
{

// aka scan window, 0.625 ms per count
template <typename T = uint16_t>
using baseband_slot = std::chrono::duration<T, std::ratio<625, 1'000'000>>;

// 1.25 ms per count
template <typename T = uint16_t>
using connection_interval = std::chrono::duration<T, std::ratio<1250, 1'000'000>>;

template <uint8_t TYPE, typename T = std::nullptr_t>
struct ad_struct : public ::bt_data
{
    explicit ad_struct(const T& arg)
        requires(std::is_integral_v<T> and
                 ((std::endian::native == std::endian::little) or (std::is_same_v<T, uint8_t>)))
    {
        this->type = TYPE;
        this->data_len = sizeof(T);
        this->data = reinterpret_cast<const uint8_t*>(&arg);
    }

    explicit ad_struct(std::initializer_list<T> args)
        requires(std::is_integral_v<T> and
                 ((std::endian::native == std::endian::little) or (std::is_same_v<T, uint8_t>)))
    {
        this->type = TYPE;
        this->data_len = sizeof(T) * args.size();
        this->data = reinterpret_cast<const uint8_t*>(args.begin());
    }

    explicit ad_struct(std::string_view str)
    {
        this->type = TYPE;
        this->data_len = str.size();
        this->data = reinterpret_cast<const uint8_t*>(str.data());
    }

    explicit ad_struct(const uuid& arg)
        requires((TYPE == BT_DATA_UUID16_SOME) or (TYPE == BT_DATA_UUID16_ALL) or
                 (TYPE == BT_DATA_UUID32_SOME) or (TYPE == BT_DATA_UUID32_ALL) or
                 (TYPE == BT_DATA_UUID128_SOME) or (TYPE == BT_DATA_UUID128_ALL))
    {
        this->type = TYPE;
        if constexpr ((TYPE == BT_DATA_UUID16_SOME) or (TYPE == BT_DATA_UUID16_ALL))
        {
            if (arg.type == BT_UUID_TYPE_16)
            {
                this->data = reinterpret_cast<const uint8_t*>(&BT_UUID_16(&arg)->val);
                this->data_len = sizeof(BT_UUID_16(&arg)->val);
            }
        }
        else if constexpr ((TYPE == BT_DATA_UUID32_SOME) or (TYPE == BT_DATA_UUID32_ALL))
        {
            if (arg.type == BT_UUID_TYPE_32)
            {
                this->data = reinterpret_cast<const uint8_t*>(&BT_UUID_32(&arg)->val);
                this->data_len = sizeof(BT_UUID_32(&arg)->val);
            }
        }
        else if constexpr ((TYPE == BT_DATA_UUID128_SOME) or (TYPE == BT_DATA_UUID128_ALL))
        {
            if (arg.type == BT_UUID_TYPE_128)
            {
                this->data = (BT_UUID_128(&arg)->val);
                this->data_len = sizeof(BT_UUID_128(&arg)->val);
            }
        }
        this->data_len = 0;
    }
};

template <std::size_t N>
constexpr auto to_adv_data(::bt_data (&data)[N])
{
    return std::to_array(data);
}
template <std::size_t N>
constexpr auto to_adv_data(::bt_data (&&data)[N])
{
    return std::to_array(data);
}

class advertisement : public ::bt_le_adv_param
{
  public:
    using options = ::bt_le_adv_opt;

    // Directed connectable
    constexpr static advertisement directed(const ::bt_addr_le_t& peer_addr,
                                            options opts = options::BT_LE_ADV_OPT_NONE)
    {
        return advertisement(BT_LE_ADV_OPT_CONN | opts, 0, 0, &peer_addr);
    }
    constexpr static advertisement directed_low_duty(const ::bt_addr_le_t& peer_addr,
                                                     options opts = options::BT_LE_ADV_OPT_NONE)
    {
        return advertisement(BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_DIR_MODE_LOW_DUTY | opts,
                             BT_GAP_ADV_FAST_INT_MIN_2, BT_GAP_ADV_FAST_INT_MAX_2, &peer_addr);
    }

    // Scannable and connectable
    template <typename T, typename MinRatio, typename MaxRatio>
    constexpr static advertisement
    connectable(std::chrono::duration<T, MinRatio> interval_min =
                    baseband_slot<uint16_t>(BT_GAP_ADV_FAST_INT_MIN_1),
                std::chrono::duration<T, MaxRatio> interval_max =
                    baseband_slot<uint16_t>(BT_GAP_ADV_FAST_INT_MAX_1))
    {
        return advertisement(BT_LE_ADV_OPT_CONN,
                             std::chrono::round<baseband_slot<uint16_t>>(interval_min).count(),
                             std::chrono::round<baseband_slot<uint16_t>>(interval_max).count());
    }

    // Non-connectable and scannable
    template <typename T, typename MinRatio, typename MaxRatio>
    constexpr static advertisement scannable(std::chrono::duration<T, MinRatio> interval_min =
                                                 baseband_slot<uint16_t>(BT_GAP_ADV_FAST_INT_MIN_1),
                                             std::chrono::duration<T, MaxRatio> interval_max =
                                                 baseband_slot<uint16_t>(BT_GAP_ADV_FAST_INT_MAX_1))
    {
        return advertisement(BT_LE_ADV_OPT_SCANNABLE,
                             std::chrono::round<baseband_slot<uint16_t>>(interval_min).count(),
                             std::chrono::round<baseband_slot<uint16_t>>(interval_max).count());
    }

    // Non-connectable and non-scannable
    template <typename T, typename MinRatio, typename MaxRatio>
    constexpr static advertisement
    non_connectable(std::chrono::duration<T, MinRatio> interval_min =
                        baseband_slot<uint16_t>(BT_GAP_ADV_FAST_INT_MIN_1),
                    std::chrono::duration<T, MaxRatio> interval_max =
                        baseband_slot<uint16_t>(BT_GAP_ADV_FAST_INT_MAX_1),
                    options opts = options::BT_LE_ADV_OPT_NONE)
    {
        return advertisement(opts,
                             std::chrono::round<baseband_slot<uint16_t>>(interval_min).count(),
                             std::chrono::round<baseband_slot<uint16_t>>(interval_max).count());
    }

    template <typename T, typename MinRatio, typename MaxRatio>
    constexpr advertisement(options opts, std::chrono::duration<T, MinRatio> interval_min,
                            std::chrono::duration<T, MaxRatio> interval_max)
        : advertisement(opts, std::chrono::round<baseband_slot<uint16_t>>(interval_min).count(),
                        std::chrono::round<baseband_slot<uint16_t>>(interval_max).count())
    {}

    c2usb::result start(const std::span<const ::bt_data>& ad = {},
                        const std::span<const ::bt_data>& sd = {}) const
    {
        return c2usb::result(::bt_le_adv_start(this, ad.data(), ad.size(), sd.data(), sd.size()));
    }

    static c2usb::result update_data(const std::span<const ::bt_data>& ad = {},
                                     const std::span<const ::bt_data>& sd = {})
    {
        return c2usb::result(::bt_le_adv_update_data(ad.data(), ad.size(), sd.data(), sd.size()));
    }

    static c2usb::result stop() { return c2usb::result(::bt_le_adv_stop()); }

  private:
    constexpr advertisement(uint8_t options, uint16_t interval_min, uint16_t interval_max,
                            const ::bt_addr_le_t* peer_addr = nullptr)
        : bt_le_adv_param{.id = BT_ID_DEFAULT,
                          .sid = 0,
                          .secondary_max_skip = 0,
                          .options = options,
                          .interval_min = interval_min,
                          .interval_max = interval_max,
                          .peer = peer_addr}
    {}
};

} // namespace bluetooth
