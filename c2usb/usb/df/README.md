# USB Device Framework (usb::df)

`usb::df` namespace hosts the c2 USB Device Framework implementation,
while other `usb` namespaces are for shared device-host definitions.
Its architecture is separated along object class boundaries,
so let's look at each.

## MAC: Medium Access Control

The `usb::df::mac` class implements access to the USB bus, providing bus state and data flow management.
Its subclasses are responsible for directly interacting with the hardware itself.
If you want to port this library to run on your hardware platform,
this is where you start.

## Functions

In USB terms, a function is what provides a meaningful device service to the host,
that is often abstracted away from the USB transfer medium itself
(such as an audio stream, a storage medium, etc).
A `usb::df::function` is the base class for USB functions.
Subclasses most often implement various USB class specification based functions,
so in most other USB stacks they are referred to as classes themselves.

USB functions are loosely coupled to their endpoints: while their operation
requires the presence and order of specific types of endpoints,
the endpoint properties such as address, max packet size or interval
are specified in the configuration. Functions access this information
to generate their configuration descriptor set, and to open the endpoints.
Opened endpoints are manipulated through their opaque handles instead of their address.

## Configurations

The `usb::df::config` namespace contains the functionality necessary for creating
and viewing USB device configurations. A configuration is a contiguous array of
`element` slots. Each slot stores one configuration item: a `header`, an `interface`,
an `endpoint`, or a `footer`. The header is always first and stores configuration
power properties and an optional name; the footer is always last and marks the end.

For example, a configuration with two interfaces can be pictured like this:

```text
+--------------+
| header       |
+--------------+
| interface    |
+--------------+
|  \_ endpoint |
+--------------+
|  \_ endpoint |
+--------------+
| interface    |
+--------------+
| footer       |
+--------------+
```

The diagram is schematic: an interface may have zero or more endpoints, and each
endpoint is associated with the interface that precedes it. Functions use one or
more interfaces and their endpoints. Conventionally, each function provides its
own `config_entry()` helper to create the required sequence of elements.

To create a configuration statically, use `make_config`, while the `builder` class
offers the possibility to dynamically rearrange a configuration.

Each configuration must exist unchanged while it is assigned to an open `device`.
Configurations are accessed through various `view`s to perform a wide range of operations:
- The `device` dispatches configuration events and interface control messages through `interface_view`.
- The `function`s use `interface_endpoint_view` to get the endpoints for each of their interfaces.
- The `mac` uses `active_endpoint_view` to allocate the resources for the endpoints to be used in the selected configuration.

## Device

The `usb::df::device` class implements chapter 9 of the USB standard specification.
Specifically, device is the root control `message` handler, either serving the requests
itself, or dispatching them to the selected interface (`function`) or vendor extension.
All USB descriptors are constructed in place in the fixed size control buffer
owned by the `mac` - therefore it is important to size this buffer to fit the largest
descriptor, be it configuration, string or BOS. Use a debug build with `assert()` enabled to verify.

A `device` is instantiated through the `usb::df::device_instance<SPEEDS, MAX_CONFIG_LIST_SIZE>` template subclass.
Constructing requires a `mac` instance, a `product_info` for constructing a device descriptor,
and a set of configurations. Each supported bus speed needs its own configurations to be set in the device.
This must be done while the device is closed (no communication with the host).

Although most application use a single configuration per speed, the device supports
using multiple alternative configurations at each speed. In this case use `make_config_list` to create
a reference list of configurations, and use `device`'s `set_configs_for_speed()` instead of `set_config_for_speed()`.

Due to the highly flexible arrangement of device functionality, string descriptor indexing
gets complicated. String owning components (`function`s, `vendor::extension`s) must indicate the number
of owned strings, and the `device` assigns string indeces when a configuration is provided
by allowing each to reserve their own indeces in this order: `function`, `vendor::extension`, configuration,
`device` level strings.

## Vendor extensions

The `usb::df::vendor::extension` subclasses allow extending the device level behavior
(instead of providing `function` scope functionality):

- device level vendor specific control message handling
- extending device BOS (Binary Object Store) descriptor
- extra string ownership
- providing alternate configuration(s)

Microsoft OS 2.0 descriptors (including alternate enumeration) and WebUSB support
are implemented through this extension model.
