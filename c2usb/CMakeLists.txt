add_library(c2usb)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/manifest_deps.cmake)
manifest_deps_add(
    MANIFEST    ${CMAKE_CURRENT_LIST_DIR}/../west.yml
    PROVIDED    ${C2USB_SDK_PROVIDED_DEPENDENCIES}
    # SDK specific dependencies are imported through their west module
    SKIP_GROUPS zephyr mcux
)

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
            usb/serial_buffer.cpp
    )
endif()
if(CONFIG_C2USB_I2C_HID)
    target_sources(c2usb
        PRIVATE
            i2c/hid/device.cpp
    )
endif()
