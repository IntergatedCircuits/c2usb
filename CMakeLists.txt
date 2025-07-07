cmake_minimum_required(VERSION 3.19)

project(c2usb
    LANGUAGES C CXX
)

if(DEFINED ENV{ZEPHYR_BASE})
    set(ZEPHYR_BUILD ON)
else()
    set(ZEPHYR_BUILD OFF)
endif()
set(ZEPHYR_BUILD ${ZEPHYR_BUILD} CACHE BOOL "Indicates if this is a Zephyr build")
if(ZEPHYR_BUILD)
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

# if using MCUXpresso SDK for VSCode, take over the includes
if(MCUX_SDK_PROJECT_NAME)
    get_target_property(MCUX_SDK_PROJECT_INCLUDE_DIRS ${MCUX_SDK_PROJECT_NAME} INCLUDE_DIRECTORIES)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${MCUX_SDK_PROJECT_INCLUDE_DIRS}
    )
endif()

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

