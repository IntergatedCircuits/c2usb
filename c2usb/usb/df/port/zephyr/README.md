# Zephyr-RTOS middleware port

This USB device MAC port builds on top of the Zephyr UDC drivers.

## 1. Project setup

These are the list of changes to your app's `CMakeLists.txt`
to get the library compiled into your project:
(Statically linking a library into Zephyr is harder than it should be,
if you solve it send us a merge request!)

```
# C headers compiled with C++ give pointer conversion errors, turn them to warnings
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fpermissive")

# Add the c2usb sources, and the support libraries
add_subdirectory(${C2USB_PATH}/c2usb)
add_subdirectory(${C2USB_PATH}/modules/magic_enum)
add_subdirectory(${C2USB_PATH}/modules/etl)
add_subdirectory(${C2USB_PATH}/modules/hid-rp)

# Add the c2usb sources to your app build
target_sources(app PRIVATE
    ${C2USB_PUBLIC_HEADERS}
    ${C2USB_PRIVATE_HEADERS}
    ${C2USB_SOURCES}
)

# Add c2usb include path
target_include_directories(app PUBLIC
    c2usb/c2usb
)

# Link to c2usb's dependencies
target_link_libraries(app PRIVATE
    magic_enum
    etl
    hid-rp
)
```

## 2. Configuration

The following are required Zephyr Kconfig options:
```
CONFIG_CPLUSPLUS=y
CONFIG_LIB_CPLUSPLUS=y
CONFIG_STD_CPP20=y

CONFIG_UDC_DRIVER=y
# find the driver for your MCU
# CONFIG_UDC_NRF=y

# RAM optimization:
# the buffer pool size can be cut down, as it's only used for control transfers
# CONFIG_UDC_BUF_POOL_SIZE=256
# CONFIG_UDC_BUF_COUNT=3 + maximal used endpoint count in a configuration
```

## 3. Integrate the MAC

A `usb::df::zephyr::udc_mac` subclass object will be the glue that connects the Zephyr OS to this library.
Create this object and pass it the devicetree object that represents the USB device, e.g.
```
usb::df::zephyr::udc_mac mac {DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0))};
```

## 4. Your turn

If your porting journey took any unexpected steps, do let us know so we can share this knowledge with others.
