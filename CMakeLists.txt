cmake_minimum_required(VERSION 3.21)

project(c2usb
    LANGUAGES CXX
)

# this library only works with C++20 and above
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

add_subdirectory(${PROJECT_NAME})

option(C2USB_TESTS "c2usb: enable unit testing" ${PROJECT_IS_TOP_LEVEL})
if(C2USB_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
