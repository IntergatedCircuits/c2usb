if (NOT CONFIG_C2USB)
    message("build without c2usb")
    return()
endif()

add_library(c2usb)

set(WORKSPACE_TOPDIR "${CMAKE_CURRENT_LIST_DIR}/../..")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../cmake")
include(get_cpm)

# ports
if(ZEPHYR_BASE)
    # compile-time string support
    CPMAddPackage("gh:intel/cpp-std-extensions#9767ab7dae3924531fcf629983377d9f35dff08b")
    if(CMAKE_CXX_COMPILER_FORCED)
        # CMake skipped compile-feature detection for this toolchain (for example, Zephyr's
        # cross-compiler sets CMAKE_CXX_COMPILER_FORCED and leaves the feature list empty), so any
        # target-level feature requirements need to be cleared after dependency creation instead.
        # This keeps the actual dialect selection to the toolchain/Kconfig choice while avoiding
        # stale or empty upstream feature metadata from nested project() calls.
        foreach(target stdx boost_mp11 concurrency)
            if(TARGET ${target})
                set_target_properties(${target} PROPERTIES INTERFACE_COMPILE_FEATURES "")
            endif()
        endforeach()
    endif()

    target_link_libraries(app PRIVATE c2usb)
    target_link_libraries(c2usb
        PUBLIC      zephyr_interface
                    stdx
        INTERFACE   kernel
    )
    # prevent build errors from late generated zephyr/heap_constants.h
    add_dependencies(c2usb zephyr_generated_headers)

    add_subdirectory(bluetooth)
    add_subdirectory(usb/df/vendor/zephyr)

    target_compile_definitions(zephyr_interface INTERFACE
        # set the standard compiler flag based on zephyr Kconfig, so e.g. assert() works as intended
        $<IF:$<BOOL:${CONFIG_DEBUG}>,DEBUG,NDEBUG>
    )
elseif(MCUX_SDK_PROJECT_NAME)
    target_link_libraries(${MCUX_SDK_PROJECT_NAME} PRIVATE c2usb)
    add_subdirectory(usb/df/vendor/nxp)
endif()

CPMAddPackage("gh:Neargye/magic_enum@0.9.3")

if(EXISTS "${WORKSPACE_TOPDIR}/modules/lib/bitfilled/CMakeLists.txt")
    set(CPM_bitfilled_SOURCE "${WORKSPACE_TOPDIR}/modules/lib/bitfilled")
endif()
if(ZEPHYR_HID_RP_MODULE_DIR OR MCUX_HID_RP_MODULE_DIR)
else()
    if(EXISTS "${WORKSPACE_TOPDIR}/modules/lib/hid-rp/CMakeLists.txt")
        set(CPM_hid-rp_SOURCE "${WORKSPACE_TOPDIR}/modules/lib/hid-rp")
    endif()
    CPMAddPackage("gh:IntergatedCircuits/hid-rp#894bd1f49c44bfbd20b44e6fd22ccf961a2f46db")
endif()

if(ZEPHYR_ETL_MODULE_DIR OR MCUX_ETL_MODULE_DIR)
else()
    if(EXISTS "${WORKSPACE_TOPDIR}/modules/lib/etl/CMakeLists.txt")
        set(CPM_etl_SOURCE "${WORKSPACE_TOPDIR}/modules/lib/etl")
    endif()
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
)
if(CONFIG_C2USB_DF)
    target_sources(c2usb
        PRIVATE
            usb/class/dfu.cpp
            usb/df/class/cdc.cpp
            usb/df/class/cdc_acm.cpp
            usb/df/class/cdc_ncm.cpp
            usb/df/class/dfu.cpp
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
endif()
if(CONFIG_C2USB_I2C_HID)
    target_sources(c2usb
        PRIVATE
            i2c/hid/device.cpp
    )
endif()
