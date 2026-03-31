add_library(c2usb-examples INTERFACE)
target_include_directories(c2usb-examples
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(c2usb-examples INTERFACE
    c2usb
)
