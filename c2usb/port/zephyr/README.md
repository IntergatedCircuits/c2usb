# Zephyr-RTOS middleware port

This USB device MAC port builds on top of the Zephyr UDC drivers.

## 1. Project setup

These are the list of changes to your app's `CMakeLists.txt`
to get the library compiled into your project:

```
# C headers compiled with C++ give pointer conversion errors, turn them to warnings
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fpermissive")

# TODO set the relative path of c2usb project
set(C2USB_PATH "c2usb")

add_subdirectory(${C2USB_PATH})

# link the application to c2usb
target_link_libraries(app PRIVATE
    c2usb
)
```

In addition, include the library's Kconfig from your application's root Kconfig
(assuming c2usb subpath):
```
rsource "c2usb/c2usb/port/zephyr/Kconfig"
```

## 2. Configuration

The following are required Zephyr Kconfig options:
```
CONFIG_STD_CPP20=y

# find the driver for your MCU
# CONFIG_UDC_NRF=y

# RAM optimization:
# the buffer pool size can be cut down, as it's only used for control transfers
# CONFIG_UDC_BUF_POOL_SIZE=optimize based on your application (and check asserts)
# CONFIG_UDC_BUF_COUNT=3 + maximal used endpoint count in a configuration

# to enable HID over GATT support:
# CONFIG_C2USB_BLUETOOTH
```

## 3. Integrate the MAC

A `usb::df::zephyr::udc_mac` subclass object will be the glue that connects the Zephyr OS to this library.
Create this object and pass it the devicetree object that represents the USB device, e.g.
```
usb::df::zephyr::udc_mac mac {DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0))};
```

## 4. Your turn

If your porting journey took any unexpected steps, do let us know so we can share this knowledge with others.
