cmake_minimum_required(VERSION 3.19)

project(c2usb
    LANGUAGES C CXX
)

if(ENV{ZEPHYR_BASE})
    if (NOT CONFIG_C2USB_CPP)
        message("Zephyr build without c2usb")
        return()
    endif()
    # using zephyr's C++ settings via zephyr_interface
else()
    # this library only works with C++20 (and above?)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS ON)
endif()

add_library(${PROJECT_NAME})
add_subdirectory(${PROJECT_NAME})

find_package(Git REQUIRED)

# fetch all submodules explicitly as there are nested ones
execute_process(
    COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND_ERROR_IS_FATAL ANY
)

add_subdirectory(modules/magic_enum)
add_subdirectory(modules/etl)
add_subdirectory(modules/hid-rp)

target_link_libraries(${PROJECT_NAME} PUBLIC
    magic_enum
    etl
    hid-rp
)

add_subdirectory(examples)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(IS_TOPLEVEL_PROJECT TRUE)
else()
    set(IS_TOPLEVEL_PROJECT FALSE)
endif()

option(C2USB_TESTS "c2usb: enable unit testing" ${IS_TOPLEVEL_PROJECT})
if(C2USB_TESTS)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
    enable_testing()
    add_subdirectory(test)
endif()
