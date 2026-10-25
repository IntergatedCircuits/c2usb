# Bluetooth support

What does Bluetooth have to do with a USB project? In this case, they are both transports
of **HID** (Human Interface Device) interface, so the primary objective is to support
a transport-agnostic HID application API, that works with both USB and BLE.
The Bluetooth implementation is only a front-end on top of the zephyr BT stack.

## HID over GATT Service

HID over GATT (HoGP) service support is implemented,
with optional Boot Keyboard and Mouse and Shorter Connection Intervals (SCI) features.

Each service instance is created with the correct number of attributes,
which is calculated at compile time directly from the HID report descriptor / report map,
using the [`hid-rp`](https://github.com/IntergatedCircuits/hid-rp) parser:
```c++
    using namespace bluetooth::hid_over_gatt;
    static service_instance<my_hid_app::report_map()>
        hogs{my_hid_app::handle(), bluetooth::security::L4_LE_SC, flags::NORMALLY_CONNECTABLE};

    static const STRUCT_SECTION_ITERABLE(bt_gatt_service_static, hogs_section) =
        hogs.static_service();
```

This unique method of sizing the attributes using C++ template parameters
allows any number of services to be used with different report maps and feature sets.

Each BLE connection that interacts with the service will get a separate HID application session
created for it - allowing parallel operation with multiple hosts.

## Device Information Service

The Device Information Service (DIS) support is implemented,
using the same product information object as the USB device.
The service instance enables the necessary attributes based on the template parameters
instead of Kconfig options. The security level is configurable - unlike zephyr's mainline DIS -
as the HOGP Device security requirements mandate a matching security level of all
profile services to that of HID service (chapter 8.1 of HOGP v1.2).

## Advertising

BLE advertisement data construction and advertisement control are supported
(primarily because zephyr's C macros are incompatible with C++).
