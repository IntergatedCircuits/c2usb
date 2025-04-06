# NXP MCUXpresso SDK USB middleware port

This USB device MAC port builds on top of the NXP USB middleware, keeping the hardware specific abstraction layer. There are two supported ways of integrating this library into a project:
1. MCUXpresso for Visual Studio Code
2. MCUXpresso IDE
A step-by-step guide for getting started follows:

## MCUXpresso for Visual Studio Code

### 1. Add c2usb to the cmake configuration

One possibility is to symlink or submodule c2usb in the `armgcc` directory
(which is the cmake project root folder), and include in the CMakeLists.txt there.
Add these lines after `target_link_libraries(${MCUX_SDK_PROJECT_NAME} PRIVATE ${TARGET_LINK_SYSTEM_LIBRARIES})`:
```
add_subdirectory(c2usb)
target_link_libraries(${MCUX_SDK_PROJECT_NAME} PRIVATE c2usb)
```

Additionally, modify the project type and C++ language support in the same file:
```
project(new_project
    LANGUAGES ASM C CXX
)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
```

### 2. Select dependent MCUXpresso components

The following components must be enabled in the `config.cmake` file:
```
# this is depending on the platform
set(CONFIG_USE_middleware_usb_device_khci true)
set(CONFIG_USE_middleware_usb_device_khci_config_header true)

set(CONFIG_USE_middleware_usb_device_common_header true)
set(CONFIG_USE_middleware_usb_common_header true)
set(CONFIG_USE_component_osa true)
set(CONFIG_USE_component_osa_bm true)
set(CONFIG_USE_component_lists true)
set(CONFIG_USE_component_osa_template_config true)
```

The rest of the steps are identical, so continue with step 3 below.

## MCUXpresso IDE

### 1. Project setup

These are the list of steps to take to get the library compiled into your project:

1. Create an SDK project where NXP's own stack is working.
2. Replace the build toolchain to one that supports C++20.
3. Add this library to the build.
4. Exclude / remove `usb_device_dci.c` (also optionally `usb_device_ch9.c` and the class drivers) from the build.

### 2. Configuration

No port-specific configuration options.

### 3. Integrate the MAC

A `usb::df::nxp::mcux_mac` subclass object (khci/ehci/lpcip3511/dwc3) will be the glue that connects the NXP firmware to this library.
Create this object (Meyer's singleton pattern is recommended),
and replace the USB interrupt handler's contents to `your_mac_instance().handle_irq();`.

### 4. Remote wakeup patch

> Only relevant if your device will use remote wakeup signalling.

NXP's AN5385 explains that the remote wakeup signalling must be between 1 and 15 milliseconds.
Yet their software design wastes a hardware timer for this purpose (alongside software anti-patterns).
To use this library, the following code sections must be replaced
(look for the `USB_DEVICE_CONFIG_REMOTE_WAKEUP` preprocessor block):
```
    startTick = deviceHandle->hwTick;
    while ((deviceHandle->hwTick - startTick) < 10U)
    {
        __NOP();
    }
```
to this instead:
```
    SDK_DelayAtLeastUs(3000, SystemCoreClock);
```
Do note that this patch is using NXP's own drivers, so this patch should get upstreamed when possible.

### 5. Flash memory access for USB DMA

In some of the chip designs the USB's DMA controller doesn't have access to read from the read-only flash memory.
NXP solves this by copying everything into RAM variables.
We will solve this by properly configuring the memory controller.
This is so far only implemented for (M)K22F MCUs,
please send a merge request when you find out the missing configuration for your MCU.

## Feedback

If your porting journey took any unexpected steps, do let us know so we can share this knowledge with others.
