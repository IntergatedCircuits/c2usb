cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(udc_power_state_device)

target_sources(app PRIVATE src/main.cpp)