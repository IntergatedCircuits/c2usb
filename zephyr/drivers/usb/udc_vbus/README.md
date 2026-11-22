# UDC VBUS sense

Reports the VBUS state of a Zephyr USB device controller (UDC) from a GPIO, for UDC drivers that
cannot detect VBUS themselves (`udc_device_caps.can_detect_vbus` is false, e.g. STM32, ESP32 and
generic DWC2). The state changes are submitted to the UDC event callback as `UDC_EVT_VBUS_READY` and
`UDC_EVT_VBUS_REMOVED`, exactly as a UDC driver with VBUS detection would do.

`udc_mac` uses it automatically, no application code is needed.

## Usage

Add the input pin (active when VBUS is present) to the UDC node in the devicetree:

```dts
&zephyr_udc0 {
    vbus-gpios = <&gpioa 9 GPIO_ACTIVE_HIGH>;
};
```

`CONFIG_C2USB_UDC_VBUS` is enabled by default if the node labelled `zephyr_udc0` has the property.
For any other label, or for more than one controller, enable it manually.
The `vbus-gpios` property doesn't have to be declared by the binding of the controller,
the `-gpios` suffix is enough for the devicetree tooling to generate it.

| Option                                | Description                                         |
|---------------------------------------|-----------------------------------------------------|
| `CONFIG_C2USB_UDC_VBUS`               | Enable the driver                                   |
| `CONFIG_C2USB_UDC_VBUS_DEBOUNCE_MS`   | Level must be stable this long before it is reported. 0 (default) reports every edge directly from the GPIO interrupt. |
| `CONFIG_C2USB_UDC_VBUS_LOG_LEVEL_*`   | Log level                                           |

## API

Only needed when not using `udc_mac`. Include `drivers/usb/udc_vbus.h`.

| Function                | Description                                                              |
|-------------------------|--------------------------------------------------------------------------|
| `udc_vbus_available()`  | The UDC has a `vbus-gpios` pin, and its driver cannot detect VBUS        |
| `udc_vbus_present()`    | Reads the pin, can be called before the UDC is initialized               |
| `udc_vbus_enable()`     | Arms the pin interrupt and reports the current state                     |
| `udc_vbus_disable()`    | Disarms the pin interrupt                                                |

Call `udc_vbus_enable()` after `udc_init()`, as that sets the event callback, and
`udc_vbus_disable()` before `udc_shutdown()`. The current state is reported on every enable,
so changes that happened while the UDC was shut down are not lost.

## Notes

- If the UDC driver can detect VBUS, the pin is ignored and never configured.
- The pin is read in interrupt context, so its GPIO controller must be accessible without
  sleeping (not an I2C expander), and it must support interrupts on both edges.
- Events are submitted with `udc_submit_event()`, declared in the private header of the Zephyr UDC
  drivers (`drivers/usb/udc/udc_common.h`), so this code may need to follow Zephyr changes.
- Test: `zephyr/tests/usb/udc_vbus` (native_sim, GPIO emulator).
