if(NOT ZEPHYR_BASE)
    # standalone STM32Cube build: the driver is compiled as part of the target linking
    # this library, so it uses the target's STM32Cube HAL include paths and definitions
    add_library(c2usb-i2c-stm32 INTERFACE)
    target_sources(c2usb-i2c-stm32 INTERFACE ${CMAKE_CURRENT_LIST_DIR}/hal_i2c_slave.cpp)
    target_link_libraries(c2usb-i2c-stm32 INTERFACE c2usb)
    return()
endif()

# Zephyr build of the STM32 HAL I2C target driver
if(NOT CONFIG_C2USB_I2C_STM32)
    return()
endif()

# the HAL headers are included through the stm32header module's st/stm32hal.h
if(NOT ZEPHYR_STM32HEADER_MODULE_DIR)
    message(FATAL_ERROR "c2usb STM32 I2C target driver requires the stm32header module "
        "(https://github.com/IntergatedCircuits/stm32header), add it to the west manifest")
endif()

target_sources(c2usb
    PRIVATE
        hal_i2c_slave.cpp
        zephyr_i2c_slave.cpp
)

# decide whether the HAL I2C driver is built here, or by the hal_stm32 module (see stm32_hal_i2c.c)
set(hal_i2c_definitions)
if(CONFIG_C2USB_I2C_STM32_V1)
    # disabled in the series' hal_conf.h, so hal_stm32 doesn't build it
elseif(CONFIG_C2USB_I2C_STM32_HAL_TARGET_ONLY AND NOT CONFIG_USE_STM32_HAL_I2C)
    list(APPEND hal_i2c_definitions C2USB_STM32_HAL_I2C_TARGET_ONLY)
else()
    return()
endif()

# Locate the HAL I2C driver source of the SoC series in the hal_stm32 module.
# The series directory and file prefix aren't derived uniformly from CONFIG_SOC_SERIES:
#   stm32f4x  -> stm32cube/stm32f4xx/drivers/src/stm32f4xx_hal_i2c.c
#   stm32wbax -> stm32cube/stm32wbaxx/drivers/src/stm32wbaxx_hal_i2c.c
#   stm32wb0x -> stm32cube/stm32wb0x/drivers/src/stm32wb0x_hal_i2c.c
# so strip the trailing 'x' of the series name, try the common "xx" suffix first,
# then fall back to the single "x" one.
string(REGEX REPLACE "x$" "" series "${CONFIG_SOC_SERIES}")
set(hal_dir ${ZEPHYR_HAL_STM32_MODULE_DIR}/stm32cube/${series}xx)
if(NOT IS_DIRECTORY ${hal_dir})
    set(hal_dir ${ZEPHYR_HAL_STM32_MODULE_DIR}/stm32cube/${series}x)
endif()
# the HAL file names are prefixed with the series directory name
get_filename_component(hal_name ${hal_dir} NAME)
set(hal_i2c_source ${hal_dir}/drivers/src/${hal_name}_hal_i2c.c)
if(NOT EXISTS ${hal_i2c_source})
    message(FATAL_ERROR "STM32 HAL I2C source not found: ${hal_i2c_source}")
endif()

# The HAL source isn't compiled directly, but through a wrapper that #includes it,
# so it can define macros before the HAL code (see stm32_hal_i2c.c):
# - HAL_I2C_MODULE_ENABLED for the v1 IP series, limited to this translation unit,
#   as it would break other sources that include <zephyr/drivers/i2c.h>
# - the target only build's substitutions of the controller mode DMA ISRs
set(wrapper ${CMAKE_CURRENT_SOURCE_DIR}/stm32_hal_i2c.c)
target_sources(c2usb PRIVATE ${wrapper})
# Source file properties are scoped to the directory that sets them, while the target is compiled
# in the directory that defines it (c2usb/), so TARGET_DIRECTORY places them there.
# The HAL source path is passed as an absolute, angle bracket header name for
# '#include C2USB_STM32_HAL_I2C_SOURCE': this form needs no quote escaping through CMake,
# and the absolute path makes the include search paths irrelevant.
# OBJECT_DEPENDS makes the dependency on the HAL source explicit, as it isn't a source of the target.
set_source_files_properties(${wrapper} TARGET_DIRECTORY c2usb PROPERTIES
    COMPILE_DEFINITIONS "${hal_i2c_definitions};C2USB_STM32_HAL_I2C_SOURCE=<${hal_i2c_source}>"
    OBJECT_DEPENDS ${hal_i2c_source}
)
