# STM32 HAL I2C target driver

This directory contains an `i2c::slave` driver for STM32 microcontrollers,
which enables using the [HID over I2C](../../hid) device on them.
It's built on the STM32Cube HAL I2C and DMA drivers, operating the I2C peripheral
in target (slave) mode with DMA transfers.
Both STM32 I2C IP versions are supported:
- v1 IP (STM32F1, F2, F4, L1): configured with the bus clock frequency
- v2 IP (all other series): configured with a pre-computed `TIMINGR` register value

| File | Content |
| ---- | ------- |
| [hal_i2c_slave.hpp](hal_i2c_slave.hpp), [hal_i2c_slave.cpp](hal_i2c_slave.cpp) | The platform agnostic driver, and the HAL I2C callbacks |
| [zephyr_i2c_slave.hpp](zephyr_i2c_slave.hpp), [zephyr_i2c_slave.cpp](zephyr_i2c_slave.cpp) | Zephyr device driver, configured from the devicetree |
| [stm32_hal_i2c.c](stm32_hal_i2c.c) | Zephyr build of the HAL I2C driver, where hal_stm32's isn't suitable |
| [CMakeLists.txt](CMakeLists.txt), [Kconfig](Kconfig) | Build integration |

## Requirements

These apply to all integrations:
- The I2C peripheral needs one DMA channel for each direction (TX and RX),
  in normal (not circular) mode, with memory increment, and byte data width on both sides.
- The I2C interrupts and both DMA channel interrupts drive the HAL I2C state machine,
  so they must have **identical priority**, preventing them from preempting each other.
- The HID application's callbacks are invoked from these interrupts.
  The application may call the HID API (e.g. `send_report()`) from any context
  that doesn't preempt them: interrupts of the same or lower priority, or thread context.
- Clock stretching must remain enabled, the driver relies on it while handling the address match.
- The DMA buffers aren't cache maintained by the driver. On cores with data cache (Cortex-M7),
  either disable the data cache, or place all buffers (the HID application's reports and
  the I2C HID device's buffer) in non-cacheable memory.

## STM32Cube integration

A standalone STM32Cube project uses the STM32CubeMX generated code to set up the peripherals,
and passes the generated I2C handle to the driver.
The [stm32-i2c-hid] project is a complete example of this integration.

### STM32CubeMX configuration

1. Enable the I2C peripheral in I2C mode. The own address is irrelevant, it's set by the driver.
   On v2 IP series, select the bus speed, and adjust the rise and fall times to your hardware,
   to have the `Timing` value calculated.
2. Add the DMA requests for I2C TX and RX in the DMA settings tab, with the settings listed above.
3. Enable the I2C event and error interrupts (single, combined line on STM32F0, G0, L0),
   and set the identical priority for them and the two DMA channels in the NVIC settings.
4. Configure the interrupt line pin as GPIO output, with an inactive initial level
   (high, as the line is typically active low).

The generated `MX_I2Cx_Init()` function must be called before the driver starts listening.
If the I2C peripheral should only be enabled when the device is created,
select *Do Not Generate Function Call* for it in *Project Manager > Advanced Settings*,
and call it right before creating the device.
The generated `HAL_I2C_MspInit()` and interrupt handlers need no changes.

### Build

Add c2usb to the project (e.g. as a git submodule), then link the driver
in the CMake project of the firmware:

```cmake
add_subdirectory(c2usb)

target_link_libraries(${PROJECT_NAME} PRIVATE
    stm32cubemx # the STM32CubeMX generated CMake library
    c2usb
    c2usb-i2c-stm32
)
```

`c2usb-i2c-stm32` is an interface library: the driver source is compiled as part of the target
linking it, so it uses the target's STM32Cube HAL include paths and definitions.
The driver includes the STM32Cube HAL of the target series through `st/stm32hal.h`
from [stm32header], so add that project to the include path as well (e.g. as a git submodule).

The HAL I2C and DMA modules must be enabled in the HAL configuration, so their sources get built
(STM32CubeMX does this, based on the peripheral configuration).

Cortex-M0 and M0+ cores lack atomic instructions, the compiler emits library calls
for atomic read-modify-write operations, which the project must implement
(see `cortex_m0_atomic.cpp` in [stm32-i2c-hid]).

### Code

The platform specific part of the driver is the interrupt line control,
which is implemented in a subclass:

```cpp
#include "i2c/hid/device.hpp"
#include "i2c/vendor/stm32/hal_i2c_slave.hpp"
#include "main.h"

extern I2C_HandleTypeDef hi2c2; // STM32CubeMX generated

class i2c_slave final : public i2c::stm32::hal_i2c_slave
{
  public:
    i2c_slave()
        : hal_i2c_slave(hi2c2)
    {}

  private:
    void set_pin_interrupt(bool asserted) override
    {
        // active low line
        HAL_GPIO_WritePin(INT_GPIO_Port, INT_Pin, asserted ? GPIO_PIN_RESET : GPIO_PIN_SET);
    }
};

void create_i2c_hid_device()
{
    static constexpr i2c::hid::product_info product_info{0x0102, 0x0304,
                                                         i2c::hid::version("1.0")};
    static i2c_slave slave;
    // sized for the application's largest OUTPUT and FEATURE reports
    static auto buffer = i2c::hid::device::make_buffer<my_app::report_protocol>();
    static i2c::hid::device dev{my_app::instance(),  product_info, slave,
                                i2c::address(0x0a), 0x0001, buffer};
}
```

The HAL callbacks of target mode operation (`HAL_I2C_AddrCallback()`, `HAL_I2C_ListenCpltCallback()`,
`HAL_I2C_SlaveTxCpltCallback()`, `HAL_I2C_SlaveRxCpltCallback()`, `HAL_I2C_ErrorCallback()`)
are implemented by the driver, so the project must not define them.
They are invoked for all HAL I2C handles, and ignore the handles that aren't operated by the driver.
Therefore other I2C peripherals of the project can be used with the HAL I2C driver
in controller mode, but not in target mode, and without error callback.

The driver also works without STM32CubeMX generated code: the `i2c::stm32::hal_handles` class
provides the HAL handles, as a base class preceding `hal_i2c_slave` (see the Zephyr driver).

## Zephyr integration

The Zephyr driver is configured entirely from the devicetree, and works without multithreading
(`CONFIG_MULTITHREADING=n`) as well.
Its use is illustrated by the [i2c-hid-keyboard](../../../../zephyr/examples/i2c-hid-keyboard) example.

### Devicetree

The SoC's I2C node's compatible is overridden to select this driver instead of
the Zephyr STM32 I2C driver: `c2usb,stm32-i2c-v1-target` or `c2usb,stm32-i2c-v2-target`,
matching the SoC's `st,stm32-i2c-v1` or `st,stm32-i2c-v2` compatible.
These nodes form an `i2c-target` bus, and the HID over I2C function is described by a child node,
with the same `hid-over-i2c` compatible and properties as the Linux kernel uses
for the remote device on the controller side.
Only a single child node is supported.

```dts
&dma1 {
	status = "okay";
};

&i2c2 {
	compatible = "c2usb,stm32-i2c-v2-target";
	clock-frequency = <I2C_BITRATE_STANDARD>;
	/* STM32CubeMX computed TIMINGR value for 48 MHz I2C clock */
	timings = <48000000 I2C_BITRATE_STANDARD 0x10A35D83>;
	dmas = <&dma1 4 (STM32_DMA_PERIPH_TX | STM32_DMA_PRIORITY_HIGH)>,
	       <&dma1 5 (STM32_DMA_PERIPH_RX | STM32_DMA_PRIORITY_HIGH)>;
	dma-names = "tx", "rx";
	status = "okay";

	i2c_hid: hid@a {
		compatible = "hid-over-i2c";
		reg = <0x0a>;
		hid-descr-addr = <0x0001>;
		int-gpios = <&gpioc 5 GPIO_ACTIVE_LOW>;
	};
};
```

- `timings` (v2 IP only, required): `<periph_clock clock-frequency timing>` triplets,
  same as in the `st,stm32-i2c-v2` binding. The entry matching the actual I2C clock rate
  and `clock-frequency` is applied, initialization fails if there is none.
- `dmas`: the DMA channel cells depend on the SoC's DMA controller binding,
  see the [v1](../../../../zephyr/dts/bindings/i2c/c2usb,stm32-i2c-v1-target.yaml) and
  [v2](../../../../zephyr/dts/bindings/i2c/c2usb,stm32-i2c-v2-target.yaml) bindings for examples.
- The interrupt priorities of the I2C node and the DMA channels must be identical,
  this is verified at build time.

### Configuration

Besides `hal_stm32`, the driver requires the [stm32header] Zephyr module in the west workspace
(as in [west4zephyr.yml](../../../../west4zephyr.yml)), to include the STM32Cube HAL headers.

Enable c2usb and HID over I2C in the application's `prj.conf`,
the driver (`CONFIG_C2USB_I2C_STM32`) is enabled automatically by the devicetree:

```
CONFIG_C2USB=y
CONFIG_C2USB_I2C_HID=y
```

Further options:
- `CONFIG_C2USB_I2C_STM32_HAL_TARGET_ONLY`: v2 IP only, removes the controller mode code
  of the HAL I2C driver from the build, saving about 1 kB of code.
  Has no effect if other parts of the application use the HAL I2C driver.
- `CONFIG_C2USB_I2C_STM32_INIT_PRIORITY`: must be after the DMA driver's.
- `CONFIG_C2USB_I2C_STM32_LOG_LEVEL`

On v1 IP series, hal_stm32 disables the HAL I2C module (its definitions collide with
the Zephyr I2C API), so the driver builds it as part of c2usb.

### Code

```cpp
#include <i2c/hid/device.hpp>
#include <i2c/vendor/stm32/zephyr_i2c_slave.hpp>

#define I2C_HID_NODE DT_NODELABEL(i2c_hid)
BUILD_ASSERT(DT_ON_BUS(I2C_HID_NODE, i2c_target), "i2c_hid node must be on an I2C target bus");

int create_i2c_hid_device()
{
    auto* slave = C2USB_I2C_SLAVE_DT_GET(I2C_HID_NODE);
    if (slave == nullptr)
    {
        return -ENODEV;
    }
    static constexpr i2c::hid::product_info product_info{0x0102, 0x0304,
                                                         i2c::hid::version("1.0")};
    static auto buffer = i2c::hid::device::make_buffer<my_app::report_protocol>();
    static i2c::hid::device dev{my_app::instance(),
                                product_info,
                                *slave,
                                C2USB_I2C_HID_DT_ADDRESS(I2C_HID_NODE),
                                C2USB_I2C_HID_DT_DESCRIPTOR_REG(I2C_HID_NODE),
                                buffer};
    return 0;
}
```

### Limitations

- `CONFIG_DCACHE` isn't supported, the build fails with it.
- SoCs with DMAMUX (`CONFIG_DMAMUX_STM32`) aren't supported yet.

### Testing

The [hid_target_loopback](../../../../zephyr/tests/i2c/hid_target_loopback) twister test suite
verifies the driver and the HID over I2C device on hardware, by shorting two I2C peripherals
of the same chip, see the [tests README](../../../../zephyr/tests/README.md).

[stm32-i2c-hid]: https://github.com/IntergatedCircuits/stm32-i2c-hid
[stm32header]: https://github.com/IntergatedCircuits/stm32header
