# Integration tests using zephyr twister

How to build:

```bash
west build -b native_sim/native/64 --build-dir build usb/device_virtual
```

How to run:
```bash
../../../zephyr/scripts/twister -T usb/device_virtual -p native_sim/native/64 -v
```
