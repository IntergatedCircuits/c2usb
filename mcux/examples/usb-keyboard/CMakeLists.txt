cmake_minimum_required(VERSION 3.22.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(WEST ON) # necessary for looking up mcux modules from west workspace

include("${SdkRootDirPath}/cmake/extension/mcux.cmake")
project(
    usb-keyboard
    LANGUAGES ASM C CXX
)
include("${SdkRootDirPath}/CMakeLists.txt")

mcux_add_macro(
    CC "-DUSB_STACK_BM"
)
target_link_libraries(${PROJECT_NAME} PUBLIC
    c2usb-examples
)
mcux_add_source(
    SOURCES
        src/main.cpp
)

mcux_convert_binary(
    BINARY "${APPLICATION_BINARY_DIR}/${MCUX_SDK_PROJECT_NAME}.hex"
)
