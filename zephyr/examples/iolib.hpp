#pragma once
#include <zephyr/drivers/gpio.h>

struct leds
{
  private:
#define GPIO_SPEC_AND_COMMA(param) GPIO_DT_SPEC_GET(param, gpios),

    static inline const gpio_dt_spec led_spec[] = {
#if DT_NODE_EXISTS(DT_PATH(leds))
        DT_FOREACH_CHILD(DT_PATH(leds), GPIO_SPEC_AND_COMMA)
#endif
    };

    leds()
    {
        int err;
        for (size_t i = 0; i < ARRAY_SIZE(led_spec); i++)
        {
            err = gpio_pin_configure_dt(&led_spec[i], GPIO_OUTPUT);
            __ASSERT(err == 0, "Cannot configure LED %u gpio", i);
            gpio_pin_set_dt(&led_spec[i], false);
        }
    }
    static leds& instance()
    {
        static leds l;
        return l;
    }

  public:
    static int set(uint8_t idx, bool on)
    {
        instance();
        if (idx >= ARRAY_SIZE(led_spec))
        {
            return -EINVAL;
        }
        return gpio_pin_set_dt(&led_spec[idx], on);
    }
};
