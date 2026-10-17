cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(c2usb_usb_cdc_ncm_virtual)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fpermissive")

target_include_directories(app PRIVATE
    ${ZEPHYR_BASE}/subsys/usb/host
    ../common
)
target_sources(app PRIVATE
    src/main.cpp
    src/ncm_host_peer.cpp
    ../common/usb_host.cpp
)