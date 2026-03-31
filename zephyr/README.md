# Zephyr-RTOS support

This library integrates into zephyr builds as a west module.
The contents of this directory tree are for supporting SDKs based on the zephyr framework.

## Examples

You need a basic understanding of the [framework][zephyr], and usage of the [`west`][west] tool.
For developing within this project using **nRF Connect**,
open [zephyr.code-workspace](zephyr.code-workspace) with `vscode`.
This allows building and running the examples available in [examples](examples).
The [zephyr](../.github/workflows/zephyr.yml) github action illustrates the command line build flow.

The first step is to initialize the workspace folder (``c2usb-workspace``) where
this repository and all Zephyr modules will be cloned. Run the following
command:

```shell
west init -m https://github.com/IntergatedCircuits/c2usb c2usb-workspace -mf west4nrfsdk.yml
cd c2usb-workspace
# fetch all required dependencies and apply any patches (always run these in a chain)
west update && west patch
```

Building an example project is similarly done by the corresponding west command:

```shell
cd c2usb/zephyr/examples
west build -b nrf52840dk/nrf52840 -d usb-keyboard/build usb-keyboard
```

Once you have built the application, run the following command to flash it:

```shell
west flash -d usb-keyboard/build
```

Use `west vscode` to generate vscode C/C++ indexer configuration.
`--build` and `--debug` additional arguments create build tasks and debug launch configurations,
from a successful build:

```shell
west vscode -d usb-keyboard/build --build "usb-keyboard" --debug "usb-keyboard"
```

### usb-keyboard

A straightforward USB HID keyboard to illustrate a minimal project integration.
Use the button on the board to trigger a caps lock press, and observe as the host changes the caps lock state on the board's LED.

### usb-shell

Demonstrating USB serial port functionality with shell access to the zephyr OS.

[zephyr]: https://docs.zephyrproject.org/latest/index.html
[west]: https://docs.zephyrproject.org/latest/develop/west/index.html
