# NXP MCUXpresso SDK support

This library integrates into MCUXpresso SDK builds as a west module.
The contents of this directory tree are for supporting this SDK.

## Examples

You need a basic understanding of the [platform][mcuxpresso], and usage of the [`west`][west] tool.
For developing within this project using **MCUXpresso for VS Code**,
open [mcux.code-workspace](mcux.code-workspace) with `vscode`.
This allows building and running the examples available in [examples](examples).
The [arm-gcc](../.github/workflows/arm-gcc.yml) github action illustrates the command line build flow.

The first step is to initialize the workspace folder (``c2usb-workspace``) where
this repository and all Zephyr modules will be cloned. Run the following
command:

```shell
west init -m https://github.com/IntergatedCircuits/c2usb c2usb-workspace -mf west4mcuxsdk.yml
cd c2usb-workspace
# fetch all required dependencies and apply any patches (always run these in a chain)
west update && west patch
```

### usb-keyboard

A straightforward USB HID keyboard to illustrate a minimal project integration.
Use the button on the board to trigger a caps lock press, and observe as the host changes the caps lock state on the board's LED.

[mcuxpresso]: https://mcuxpresso.nxp.com/mcuxsdk/latest/html/index.html
[west]: https://docs.zephyrproject.org/latest/develop/west/index.html
