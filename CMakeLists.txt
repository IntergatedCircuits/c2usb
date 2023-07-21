cmake_minimum_required(VERSION 3.14)

project(c2usb LANGUAGES CXX)

include(GNUInstallDirs)

# fetch all submodules explicitly as there are nested ones
execute_process(
    COMMAND git submodule update --init --recursive
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    RESULT_VARIABLE submodule_result
)
if(NOT ${submodule_result} EQUAL 0)
    message(FATAL_ERROR "Failed to fetch Git submodules.")
endif()

# this library only works with C++20 (and above?)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}")

add_subdirectory(modules/magic_enum)
add_subdirectory(modules/etl)
add_subdirectory(modules/hid-rp)
add_subdirectory(${PROJECT_NAME})

add_library(${PROJECT_NAME}
    ${C2USB_PUBLIC_HEADERS}
    ${C2USB_PRIVATE_HEADERS}
    ${C2USB_SOURCES}
)

target_link_libraries(${PROJECT_NAME}
    magic_enum
    etl
    hid-rp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${INCLUDES}
)
