# Configurable Composite (c2) USB device library

This is a 2nd generation USB device library,
designed with an emphasis of maximum flexibility for configurability and composability.
The device framework is designed to scale well to designs of any complexity,
and to make the high level functionality as portable as possible.

The use of the C++(20) language is integral to the project's design.
That doesn't mean dynamic allocation or use of exceptions, but instead
accurate abstractions, encapsulation, type safety, polymorphism.

## Features

* USB 2.0(.1) specification compliant full and high-speed stack
* self-describing objects -> no manual implementation of any USB descriptors
* efficient RAM usage

### Platforms

The library integrates as a **west module** into the following platforms:
* NXP MCUXpresso (see [mcux](mcux) directory)
* Zephyr RTOS (see [zephyr](zephyr) directory)

### Device classes

#### HID - Human Interface Device Class

HID specification version 1.11 is fully supported, with extensive report descriptor tooling
via [hid-rp][hid-rp] library.
HID has outgrown itself from a pure USB class to a transport-independent protocol,
and so this library also provides alternative transports for HID applications,
which interact with the same high-level application API.
The additional transport layers supported are:
* BLE (HID over GATT Protocol)
* I2C

#### CDC-ACM - Serial Port

The Abstract Control Model of Communications Device Class is fully implemented.
Notably the notification endpoint can be marked as unused, skipping any hardware resource allocation,
but keeping compatibility with all hosts.

### Vendor extensions

#### Microsoft OS descriptors

Microsoft OS descriptors version 2.0 is supported.
The main motivation to support this functionality is because
MS likes to make everybody else's life difficult.
In the case of USB, this means that in many cases the USB standardized device classes
don't get the correct OS driver assigned (even if it's available on the system, such as CDC-NCM on Windows 10),
or get a downgraded driver instead (such as HID gamepads getting DirectInput driver, except if manufactured by MS, see xinputhid.inf),
or no driver at all.
The only possible solution to deal with these is using [Windows Compatible IDs][WCID].
This is stored in the USB device's descriptors, and tells Windows which driver to load for the given USB function.

#### Microsoft XBOX-360 controller interface

Microsoft XBOX-360 gamepad controller interface is implemented to leverage XInput driver on Windows
for gamepad applications without any user step. Combining this with Microsoft OS descriptors
makes it possible to have a USB device that either presents an HID or an XInput gamepad interface
towards the host computer, depending on its OS.

[project-structure]: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p1204r0.html
[hid-rp]: https://github.com/IntergatedCircuits/hid-rp
[WCID]: https://github.com/pbatard/libwdi/wiki/WCID-Devices
