if (NOT CONFIG_C2USB)
    message("build without c2usb")
    return()
endif()

add_library(c2usb)

set(WORKSPACE_TOPDIR "${CMAKE_CURRENT_LIST_DIR}/../..")

# ports
if(ZEPHYR_BASE)
    add_subdirectory(bluetooth)
    add_subdirectory(port/zephyr)
    # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fpermissive" PARENT_SCOPE) # https://github.com/zephyrproject-rtos/zephyr/pull/69490

    target_compile_definitions(zephyr_interface INTERFACE
        # set the standard compiler flag based on zephyr Kconfig, so e.g. assert() works as intended
        $<IF:$<BOOL:${CONFIG_DEBUG}>,DEBUG,NDEBUG>
    )
elseif(MCUX_SDK_PROJECT_NAME)
    add_subdirectory(port/nxp)
endif()

if(EXISTS "${WORKSPACE_TOPDIR}/modules/lib/bitfilled/CMakeLists.txt")
    set(CPM_bitfilled_SOURCE "${WORKSPACE_TOPDIR}/modules/lib/bitfilled")
endif()
if(EXISTS "${WORKSPACE_TOPDIR}/modules/lib/hid-rp/CMakeLists.txt")
    set(CPM_hid-rp_SOURCE "${WORKSPACE_TOPDIR}/modules/lib/hid-rp")
endif()
if(EXISTS "${WORKSPACE_TOPDIR}/modules/lib/etl/CMakeLists.txt")
    set(CPM_etl_SOURCE "${WORKSPACE_TOPDIR}/modules/lib/etl")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../cmake")
include(get_cpm)
CPMAddPackage("gh:Neargye/magic_enum@0.9.3")

include(../cmake/get_cpm.cmake)
if(ZEPHYR_HID_RP_MODULE_DIR OR MCUX_HID_RP_MODULE_DIR)
else()
    CPMAddPackage("gh:IntergatedCircuits/hid-rp#85d86f7fa37d3a846c3197b7b599cf7bc0c21b04")
endif()

if(ZEPHYR_ETL_MODULE_DIR OR MCUX_ETL_MODULE_DIR)
else()
    CPMAddPackage("gh:ETLCPP/etl#20.48.1")
endif()

target_link_libraries(c2usb
    PUBLIC
        magic_enum
        etl
        hid-rp
)

target_include_directories(c2usb
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_sources(c2usb
    PRIVATE
        hid/application.cpp
        usb/df/class/cdc.cpp
        usb/df/class/cdc_acm.cpp
        usb/df/class/hid.cpp
        usb/df/vendor/microsoft/os_extension.cpp
        usb/df/vendor/microsoft/xinput.cpp
        usb/df/config.cpp
        usb/df/device.cpp
        usb/df/function.cpp
        usb/df/mac.cpp
        usb/df/message.cpp
        usb/standard/descriptors.cpp
)
if(CONFIG_C2USB_I2C_HID)
    target_sources(c2usb
        PRIVATE
            i2c/hid/device.cpp
    )
endif()

add_subdirectory(examples)
