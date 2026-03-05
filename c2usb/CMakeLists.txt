if(ZEPHYR_BASE)
    if (NOT CONFIG_C2USB_CPP)
        message("Zephyr build without c2usb")
        return()
    endif()
endif()

add_library(c2usb)

set(WORKSPACE_TOPDIR "${CMAKE_CURRENT_LIST_DIR}/../..")

# ports
if(ZEPHYR_BASE)
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
    CPMAddPackage("gh:IntergatedCircuits/hid-rp#ceb9a63678114790e58ad884fe79998b952968c8")
endif()

if(ZEPHYR_ETL_MODULE_DIR OR MCUX_ETL_MODULE_DIR)
    # make etl available in any case
    if(NOT DEFINED CONFIG_ETL)
        include(FetchContent)
        FetchContent_Declare(etl SOURCE_DIR ${ZEPHYR_ETL_MODULE_DIR})
        FetchContent_MakeAvailable(etl)
    endif()
else()
    CPMAddPackage("gh:ETLCPP/etl#20.36.1")
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
    PUBLIC
        hid/application.hpp
        i2c/hid/device.hpp
        i2c/hid/standard.hpp
        i2c/base.hpp
        i2c/slave.hpp
        usb/class/hid.hpp
        usb/df/class/cdc.hpp
        usb/df/class/cdc_acm.hpp
        usb/df/class/hid.hpp
        usb/df/vendor/microsoft/os_extension.hpp
        usb/df/vendor/microsoft/xinput.hpp
        usb/df/config.hpp
        usb/df/device.hpp
        usb/df/ep_flags.hpp
        usb/df/function.hpp
        usb/df/mac.hpp
        usb/df/message.hpp
        usb/df/transfer.hpp
        usb/standard/descriptors.hpp
        usb/standard/requests.hpp
        usb/vendor/microsoft_os.hpp
        usb/vendor/microsoft_xusb.hpp
        usb/base.hpp
        usb/control.hpp
        usb/endpoint.hpp
        usb/product_info.hpp
        usb/speeds.hpp
        usb/version.hpp
        c2usb.hpp
        compatibility_helper.hpp
        raw_to_hex_string.hpp
        reference_array_view.hpp
        single_elem_queue.hpp
        uninit_store.hpp
    PRIVATE
        i2c/hid/device.cpp
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

add_subdirectory(examples)
