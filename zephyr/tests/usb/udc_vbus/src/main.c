// SPDX-License-Identifier: MPL-2.0
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/usb/udc.h>
#include <zephyr/ztest.h>

#include "drivers/usb/udc_vbus.h"

#define UDC_NODE  DT_NODELABEL(zephyr_udc0)
#define VBUS_PORT DEVICE_DT_GET(DT_NODELABEL(gpio0))
#define VBUS_PIN  7

static const struct device *const udc = DEVICE_DT_GET(UDC_NODE);

static enum udc_event_type events[8];
static unsigned event_count;

static int event_cb(const struct device *dev, const struct udc_event *event)
{
	ARG_UNUSED(dev);
	if (event_count < ARRAY_SIZE(events)) {
		events[event_count++] = event->type;
	}
	return 0;
}

static void init_udc(void)
{
	int ret = udc_init(udc, event_cb, &event_count);
	zassert_ok(ret, "udc_init failed: %d", ret);
}

static void set_vbus(int level)
{
	zassert_ok(gpio_emul_input_set(VBUS_PORT, VBUS_PIN, level));
#if CONFIG_C2USB_UDC_VBUS_DEBOUNCE_MS > 0
	/* the change is only reported after the debounce time */
	k_msleep(CONFIG_C2USB_UDC_VBUS_DEBOUNCE_MS * 2);
#endif
}

static void before(void *unused)
{
	ARG_UNUSED(unused);
	event_count = 0;
	zassert_ok(gpio_pin_configure(VBUS_PORT, VBUS_PIN, GPIO_INPUT));
	set_vbus(0);
}

static void after(void *unused)
{
	ARG_UNUSED(unused);
	udc_vbus_disable(udc);
	udc_shutdown(udc);
}

ZTEST(c2usb_udc_vbus, test_available)
{
	zassert_true(udc_vbus_available(udc));
}

ZTEST(c2usb_udc_vbus, test_present_follows_pin)
{
	zassert_false(udc_vbus_present(udc));
	set_vbus(1);
	zassert_true(udc_vbus_present(udc));
	set_vbus(0);
	zassert_false(udc_vbus_present(udc));
}

ZTEST(c2usb_udc_vbus, test_removed_reported_on_enable)
{
	init_udc();
	zassert_ok(udc_vbus_enable(udc));

	zassert_equal(event_count, 1);
	zassert_equal(events[0], UDC_EVT_VBUS_REMOVED);
}

ZTEST(c2usb_udc_vbus, test_ready_reported_on_enable)
{
	set_vbus(1);
	init_udc();
	zassert_ok(udc_vbus_enable(udc));

	zassert_equal(event_count, 1);
	zassert_equal(events[0], UDC_EVT_VBUS_READY);
}

ZTEST(c2usb_udc_vbus, test_edges_reported)
{
	init_udc();
	zassert_ok(udc_vbus_enable(udc));
	event_count = 0;

	set_vbus(1);
	set_vbus(0);
	set_vbus(1);

	zassert_equal(event_count, 3);
	zassert_equal(events[0], UDC_EVT_VBUS_READY);
	zassert_equal(events[1], UDC_EVT_VBUS_REMOVED);
	zassert_equal(events[2], UDC_EVT_VBUS_READY);
}

ZTEST(c2usb_udc_vbus, test_no_reports_after_disable)
{
	init_udc();
	zassert_ok(udc_vbus_enable(udc));
	udc_vbus_disable(udc);
	event_count = 0;

	set_vbus(1);

	zassert_equal(event_count, 0);
}

ZTEST(c2usb_udc_vbus, test_reenable_reports_current_state)
{
	init_udc();
	zassert_ok(udc_vbus_enable(udc));
	udc_vbus_disable(udc);
	zassert_ok(udc_shutdown(udc));

	set_vbus(1); // changed while shut down
	event_count = 0;
	init_udc();
	zassert_ok(udc_vbus_enable(udc));

	zassert_equal(event_count, 1);
	zassert_equal(events[0], UDC_EVT_VBUS_READY);
}

ZTEST_SUITE(c2usb_udc_vbus, NULL, NULL, before, after, NULL);
