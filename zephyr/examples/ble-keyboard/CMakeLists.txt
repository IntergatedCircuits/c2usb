cmake_minimum_required(VERSION 3.21)
set(NO_SYSTEM_INCLUDE ON)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ble-keyboard)

target_sources(app PRIVATE src/main.cpp)
