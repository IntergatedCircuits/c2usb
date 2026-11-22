// SPDX-License-Identifier: MPL-2.0
#include "drivers/usb/udc_vbus.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/usb/udc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

/* private header of the UDC drivers, for udc_submit_event() */
#include "udc_common.h"

LOG_MODULE_REGISTER(c2usb_udc_vbus, CONFIG_C2USB_UDC_VBUS_LOG_LEVEL);

#define VBUS_UNKNOWN (-1)

struct udc_vbus {
	const struct device *udc;
	struct gpio_dt_spec gpio;
	struct gpio_callback gpio_cb;
#if CONFIG_C2USB_UDC_VBUS_DEBOUNCE_MS > 0
	struct k_work_delayable work;
#endif
	/* last reported state: VBUS_UNKNOWN, 0 or 1 */
	atomic_t reported;
	bool armed;
};

/* Every enabled node with a vbus-gpios property is a sensed UDC. The property is not declared
 * in the bindings of most UDCs, but it is still generated for the "-gpios" suffix.
 * The terminating entry avoids a zero-length array when there is no such node.
 */
#define UDC_VBUS_INIT(node)                                                                        \
	COND_CODE_1(DT_NODE_HAS_PROP(node, vbus_gpios),                                                  \
              (                                                                                    \
                  {                                                                                \
                      .udc = DEVICE_DT_GET_OR_NULL(node),                                          \
                      .gpio = GPIO_DT_SPEC_GET(node, vbus_gpios),                                  \
                      .reported = ATOMIC_INIT(VBUS_UNKNOWN),                                       \
                  }, ),                                                                            \
              ())

static struct udc_vbus instances[] = {DT_FOREACH_STATUS_OKAY_NODE(UDC_VBUS_INIT){0}};

static struct udc_vbus *find(const struct device *udc)
{
	for (size_t i = 0; i < ARRAY_SIZE(instances); i++) {
		if (instances[i].udc == udc) {
			return &instances[i];
		}
	}
	return NULL;
}

/* Reports the pin level, if it differs from the last reported one. */
static void report(struct udc_vbus *vbus)
{
	int level = gpio_pin_get_dt(&vbus->gpio);
	if (level < 0) {
		LOG_ERR("Failed to read VBUS pin: %d", level);
		return;
	}
	level = level != 0;
	if (atomic_set(&vbus->reported, level) != level) {
		LOG_DBG("VBUS %s", level ? "ready" : "removed");
		udc_submit_event(vbus->udc, level ? UDC_EVT_VBUS_READY : UDC_EVT_VBUS_REMOVED, 0);
	}
}

#if CONFIG_C2USB_UDC_VBUS_DEBOUNCE_MS > 0
static void debounce_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	report(CONTAINER_OF(dwork, struct udc_vbus, work));
}
#endif

static void gpio_handler(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(pins);
	struct udc_vbus *vbus = CONTAINER_OF(cb, struct udc_vbus, gpio_cb);

#if CONFIG_C2USB_UDC_VBUS_DEBOUNCE_MS > 0
	/* each edge restarts the timer, so only a stable level is reported */
	k_work_reschedule(&vbus->work, K_MSEC(CONFIG_C2USB_UDC_VBUS_DEBOUNCE_MS));
#else
	report(vbus);
#endif
}

static int configure_input(const struct udc_vbus *vbus)
{
	if (!gpio_is_ready_dt(&vbus->gpio)) {
		return -ENODEV;
	}
	return gpio_pin_configure_dt(&vbus->gpio, GPIO_INPUT);
}

bool udc_vbus_available(const struct device *udc)
{
	return find(udc) != NULL && !udc_caps(udc).can_detect_vbus;
}

bool udc_vbus_present(const struct device *udc)
{
	struct udc_vbus *vbus = find(udc);
	if (vbus == NULL || configure_input(vbus) != 0) {
		return false;
	}
	return gpio_pin_get_dt(&vbus->gpio) > 0;
}

int udc_vbus_enable(const struct device *udc)
{
	struct udc_vbus *vbus = find(udc);
	int ret;

	if (vbus == NULL || udc_caps(udc).can_detect_vbus) {
		return -ENODEV;
	}
	ret = configure_input(vbus);
	if (ret != 0) {
		return ret;
	}

#if CONFIG_C2USB_UDC_VBUS_DEBOUNCE_MS > 0
	k_work_init_delayable(&vbus->work, debounce_handler);
#endif
	gpio_init_callback(&vbus->gpio_cb, gpio_handler, BIT(vbus->gpio.pin));
	ret = gpio_add_callback_dt(&vbus->gpio, &vbus->gpio_cb);
	if (ret != 0) {
		return ret;
	}
	atomic_set(&vbus->reported, VBUS_UNKNOWN);
	ret = gpio_pin_interrupt_configure_dt(&vbus->gpio, GPIO_INT_EDGE_BOTH);
	if (ret != 0) {
		gpio_remove_callback_dt(&vbus->gpio, &vbus->gpio_cb);
		return ret;
	}

	/* the state is reported even if there was no edge since the last udc_init() */
	vbus->armed = true;
	report(vbus);
	return 0;
}

void udc_vbus_disable(const struct device *udc)
{
	struct udc_vbus *vbus = find(udc);
	if (vbus == NULL || !vbus->armed) {
		return;
	}
	gpio_pin_interrupt_configure_dt(&vbus->gpio, GPIO_INT_DISABLE);
	gpio_remove_callback_dt(&vbus->gpio, &vbus->gpio_cb);
#if CONFIG_C2USB_UDC_VBUS_DEBOUNCE_MS > 0
	k_work_cancel_delayable_sync(&vbus->work, &(struct k_work_sync){0});
#endif
	atomic_set(&vbus->reported, VBUS_UNKNOWN);
	vbus->armed = false;
}
