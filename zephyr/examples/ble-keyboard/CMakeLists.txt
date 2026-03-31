cmake_minimum_required(VERSION 3.21)
set(NO_SYSTEM_INCLUDE ON)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ble-keyboard)

target_sources(app PRIVATE src/main.cpp)

# link the application to c2usb
target_link_libraries(app PRIVATE
    c2usb
    c2usb-examples
)
