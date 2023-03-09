# Configurable Composite USB device library

This is the 2nd generation of my composite USB device library,
which is the most flexible and configurable open-source USB device stack so far.
The device framework is designed to scale well to designs of any complexity,
and to make the high level functionality as portable as possible.

The core of the flexibility and portability is the manual high-level definition
of each USB configuration, which consists of:
1. Power configuration
2. List of USB functions with their assigned endpoints

This manual definition allows for a device framework with multiple number of configurations
for each bus speed (and even alternative configurations for MS Windows OS),
that can be easily modified at runtime as well.

This codebase is written in C++20, as it allows better abstractions, encapsulation,
type safety, etc as C. Implementing the same logic in C would have required
a considerable amount of preprocessor macros,
which would have made the library much harder to use correctly.
Thanks to C++, there is also no need for a configuration header full of preprocessor defines.
Note that the codebase has no dynamic memory allocations nor any other expensive C++
standard library dependency. Its optimized build produces a code size on par with
C libraries of similar functionality.

## Features

* USB 2.0(.1) specification compliant stack
* self-describing objects -> no manual implementation of any USB descriptors
* efficient RAM usage

### Device classes

* Human Interface Device Class (HID) specification version 1.11 (HID over I2C transport also supported)
* support the project to see more!

### Vendor extensions

* Microsoft OS descriptors version 2.0
* Microsoft XBOX-360 controller interface

### Platforms

* NXP MCUs supported via `kusb_mac`
* support the project to see more!

[project-structure]: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p1204r0.html
