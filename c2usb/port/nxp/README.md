# NXP MCUXpresso SDK USB middleware port

This USB device MAC port builds on top of the NXP USB middleware, keeping the hardware specific abstraction layer. A step-by-step guide for getting started follows:

## 1. Project configuration

1. Add c2usb git repository to the west manifest, or clone it at a convenient location
2. Set `EXTRA_MCUX_MODULES` cmake variable to the c2usb repository path in your project's root CMakeLists.txt
3. Enable the c2usb MAC support in `prj.conf`: `CONFIG_C2USB_MCUX_MAC=y`
4. Make sure you use at least C++20 (`set(CMAKE_CXX_STANDARD 20)`)

## 2. Integrate the MAC

A `usb::df::nxp::mcux_mac` object will be the glue that connects the NXP firmware to this library.
Create this object (Meyer's singleton pattern is recommended),
and replace the USB interrupt handler's contents to `your_mac_instance().handle_irq();`.

## 3. Flash memory access for USB DMA

In some of the chip designs the USB's DMA controller doesn't have access to read from the read-only flash memory.
NXP solves this by copying everything into RAM variables.
We will solve this by properly configuring the memory controller.
This is so far only implemented for (M)K22F MCUs,
please send a merge request when you find out the missing configuration for your MCU.

## Feedback

If your porting journey took any unexpected steps, do let us know so we can share this knowledge with others.
