cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(c2usb_usb_device_virtual)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fpermissive")

target_include_directories(app PRIVATE
        ${ZEPHYR_BASE}/subsys/usb/host
        ../include
)
target_sources(app PRIVATE src/main.cpp)
