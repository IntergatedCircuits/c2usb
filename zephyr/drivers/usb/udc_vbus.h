// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct device;

#ifdef CONFIG_C2USB_UDC_VBUS

/// @brief Whether VBUS of the UDC is sensed by a GPIO.
///
/// True only if the UDC devicetree node has a "vbus-gpios" property, and the UDC driver
/// cannot detect VBUS by itself.
bool udc_vbus_available(const struct device* udc);

/// @brief Reads the current VBUS state from the GPIO.
/// @return true if VBUS is present
bool udc_vbus_present(const struct device* udc);

/// @brief Starts reporting VBUS changes of the UDC as UDC_EVT_VBUS_READY and
/// UDC_EVT_VBUS_REMOVED events, and reports the current state immediately.
///
/// Must be called after udc_init() of the UDC, as that sets the event callback.
/// The events are submitted from interrupt context (or from the system workqueue
/// when debouncing is configured).
/// @return 0 on success, negative errno otherwise
int udc_vbus_enable(const struct device* udc);

/// @brief Stops reporting. Must be called before udc_shutdown() of the UDC.
void udc_vbus_disable(const struct device* udc);

#else

static inline bool udc_vbus_available(const struct device* udc)
{
    (void)udc;
    return false;
}
static inline bool udc_vbus_present(const struct device* udc)
{
    (void)udc;
    return false;
}
static inline int udc_vbus_enable(const struct device* udc)
{
    (void)udc;
    return 0;
}
static inline void udc_vbus_disable(const struct device* udc)
{
    (void)udc;
}

#endif

#ifdef __cplusplus
}
#endif
