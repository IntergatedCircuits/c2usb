# USB Device Framework (usb::df)

`usb::df` is a high-level USB device framework that separates:

1. Device policy and configuration modeling
2. USB class/function logic (HID, CDC-ACM, vendor functions)
3. Hardware/RTOS USB controller integration (MAC implementations)

The key design idea is that a USB configuration is defined manually at a high level
as a static composition of:

1. Power properties
2. Interfaces bound to function objects
3. Endpoints bound to those interfaces

The framework then uses those definitions to build descriptors and route control/data
transfers without requiring descriptor blobs handwritten by the application.
This manual definition allows for a device framework with multiple number of configurations
for each bus speed (and even alternative configurations for MS Windows OS),
that can be easily modified at runtime as well.

## Design goals

- Portable high-level USB function logic across platforms
- Explicit, static configuration composition (no hidden dynamic behavior)
- No dynamic memory requirement in the framework core
- Runtime selection of configuration sets (including per-speed lists)
- Support for vendor/device extensions without forking core device logic

## Core concepts

### 1) Function objects

A `usb::df::function` is the base class for USB functions. Subclasses implement class
behavior and descriptor contribution.

Responsibilities of a function:

- Contribute to configuration descriptor via `describe_config(...)`
- Handle control requests routed to its interfaces/endpoints
- Manage its assigned endpoints while it is active
- Optionally own string descriptor indices

Examples in-tree:

- `usb::df::hid::function`
- `usb::df::cdc::acm::function`
- `usb::df::microsoft::xfunction`

### 2) Configuration model

The `usb::df::config` configuration model is built around fixed-size elements:

- `power`: bus/self/shared power + remote wakeup + max current
- `header`: configuration metadata (`power` + optional name + computed size)
- `interface`: binds one interface entry to a function object
- `endpoint`: endpoint descriptor plus internal flags

The tests in `test/usb/df/config.test.cpp` demonstrate intended behavior,
including reverse iteration, endpoint lookup, interface endpoint views,
active-vs-unused endpoint filtering, and list handling.

### 3) Device controller

`usb::df::device` handles standard device-level control flow:

- standard requests (`GET_DESCRIPTOR`, `SET_CONFIGURATION`, etc.)
- interface and endpoint recipient request routing
- active configuration transitions
- string descriptor ownership and dispatch
- BOS descriptor assembly
- power/state event signaling to application

`usb::df::device_instance<SPEEDS, MAX_CONFIG_LIST_SIZE>` stores and serves
configuration lists per speed and provides convenience APIs for single-config devices.

### 4) MAC abstraction

`usb::df::mac` is the hardware/driver abstraction used by `device` and `function` objects.
It provides:

- bus attach/detach and reset integration
- control transfer staging
- endpoint open/close/send/receive/stall operations
- active endpoint mapping helpers

Platform ports implement concrete behavior (for example Zephyr UDC and NXP MCUX).

### 5) Extension mechanism

`usb::df::device::extension` extensions can hook device behavior without modifying core classes:

- bus reset reaction
- extra string ownership
- descriptor/control request augmentation
- speed-specific config override
- BOS capability contribution

Microsoft OS 2.0 support is implemented through this extension model.

## Architecture at a glance

Typical flow:

1. Application creates function objects
2. Application builds one or more `config::view` definitions
3. Application registers configs in `device_instance`
4. Application opens device (`device.open()`)
5. Host enumerates; `device` builds descriptors by asking each function
6. Host selects configuration; functions are initialized and endpoints opened
7. Class/data traffic is routed between `device`, `function`, and `mac`

Layering:

- Application: owns function instances and selected configuration sets
- `usb::df` core: descriptor generation, control routing, lifecycle orchestration
- Port MAC: interacts with USB controller driver/RTOS

## Practical extension points

For new class/vendor development:

1. Derive from `usb::df::function` or `usb::df::named_function`
2. Implement `describe_config(...)`
3. Implement control request handlers as needed
4. Implement enable/disable and transfer callbacks
5. Provide a `config(...)` helper to simplify integration

For device-wide customization:

1. Derive from `usb::df::device::extension`
2. Override only the hooks needed
3. Inject extension instance into `device_instance` constructor
