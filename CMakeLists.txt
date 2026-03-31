cmake_minimum_required(VERSION 3.21)

project(c2usb
    LANGUAGES CXX
)

# this library only works with C++20 and above
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# TODO: list all kconfig options that aren't dependent on SDK
set(CONFIG_C2USB ON)
set(CONFIG_C2USB_I2C_HID ON)
set(CONFIG_C2USB_HID_BOOT_PROTOCOL ON)

add_subdirectory(${PROJECT_NAME})

target_compile_definitions(${PROJECT_NAME} PUBLIC
    CONFIG_C2USB=1
    CONFIG_C2USB_I2C_HID=1
    CONFIG_C2USB_HID_BOOT_PROTOCOL=1
)

option(C2USB_TESTS "c2usb: enable unit testing" ${PROJECT_IS_TOP_LEVEL})
if(C2USB_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
