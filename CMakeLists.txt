cmake_minimum_required(VERSION 3.21)

project(c2usb
    LANGUAGES C CXX
)

if(ZEPHYR_BASE)
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

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(get_cpm)
CPMAddPackage("gh:IntergatedCircuits/hid-rp#266a533b938b4a7729856a211a92f7d4e76954c3")
CPMAddPackage("gh:Neargye/magic_enum@0.9.3")
CPMAddPackage("gh:ETLCPP/etl#20.36.1")

add_library(${PROJECT_NAME})
add_subdirectory(${PROJECT_NAME})

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        magic_enum
        etl
        hid-rp
)

add_subdirectory(examples)

option(C2USB_TESTS "c2usb: enable unit testing" ${PROJECT_IS_TOP_LEVEL})
if(C2USB_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
