add_library(c2usb-example-hid INTERFACE)
target_include_directories(c2usb-example-hid
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(c2usb-example-hid INTERFACE
    c2usb
)
