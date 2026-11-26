# Integration tests using zephyr twister

How to build and run:
```bash
west twister -T usb/device_virtual -p native_sim/native/64 -v
west twister -T usb/cdc_ncm_virtual -p native_sim/native/64 -v
```

How to generate coverage:

```bash
west twister -T usb/cdc_ncm_virtual -p native_sim/native/64 \
	-O build/coverage_ncm --coverage --coverage-basedir ../../../c2usb \
	--gcov-tool "$(gcc -print-prog-name=gcov)"
# run a HTTP server to view the output HTML
python3 -m http.server 8000 --directory ./build/coverage-ncm/coverage/
```

Twister writes the HTML report to `build/coverage_ncm/coverage/index.html` and raw coverage data to `build/coverage_ncm/coverage.json`. Use a `gcov` binary matching the host GCC version; this workspace uses GCC 14 and `/usr/bin/gcov-14`. Explicit `--gcov-tool` also avoids a Twister exception in this checkout when `ZEPHYR_TOOLCHAIN_VARIANT` is unset.

## Hardware tests

`i2c/hid_target_loopback` runs the I2C HID device on one I2C peripheral (c2usb STM32 HAL target
driver), and an I2C HID host on another I2C peripheral of the same chip (Zephyr I2C controller
driver), both using DMA. The two buses must be shorted as described in the test's
`boards/<board>.overlay`, which is the same wiring as Zephyr's `i2c_bus_short` fixture.
It has two scenarios, each with its own HID application:
- `c2usb.i2c.hid_target_loopback.keyboard`: `simple_keyboard`, typical use,
  and the Linux i2c-hid driver's device bring-up sequence
- `c2usb.i2c.hid_target_loopback.recording_app`: a test application recording every
  application and session callback, covering the session lifecycle, report IDs (including the
  extended command format), large OUTPUT and FEATURE reports, INPUT report flow control
  and cancellation, and receive buffer flow control

```bash
west twister -T i2c/hid_target_loopback -p stm32f072b_disco --device-testing \
	--device-serial /dev/ttyACM0 --fixture i2c_bus_short -v
```
