# Integration tests using zephyr twister

How to build and run:
```bash
west twister -c -T usb/device_virtual -p native_sim/native/64 -v
west twister -c -T usb/cdc_ncm_virtual -p native_sim/native/64 -v
west twister -c -T bluetooth/battery_service -p native_sim/native/64 -v
```

How to generate coverage:

```bash
west twister -c -T usb/cdc_ncm_virtual -p native_sim/native/64 \
	-O build/coverage_ncm --coverage --coverage-basedir ../../../c2usb \
	--gcov-tool "$(gcc -print-prog-name=gcov)"
# run a HTTP server to view the output HTML
python3 -m http.server 8000 --directory ./build/coverage-ncm/coverage/
```

Twister writes the HTML report to `build/coverage_ncm/coverage/index.html` and raw coverage data to `build/coverage_ncm/coverage.json`. Use a `gcov` binary matching the host GCC version; this workspace uses GCC 14 and `/usr/bin/gcov-14`. Explicit `--gcov-tool` also avoids a Twister exception in this checkout when `ZEPHYR_TOOLCHAIN_VARIANT` is unset.
