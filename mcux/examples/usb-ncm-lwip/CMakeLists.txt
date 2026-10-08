cmake_minimum_required(VERSION 3.22.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(WEST ON) # necessary for looking up mcux modules from west workspace

include("${SdkRootDirPath}/cmake/extension/mcux.cmake")
project(
    usb-ncm-lwip
    LANGUAGES ASM C CXX
)
include("${SdkRootDirPath}/CMakeLists.txt")
include("${SdkRootDirPath}/examples/lwip_examples/httpsrv_common/freertos/CMakeLists.txt")
include("${SdkRootDirPath}/examples/lwip_examples/httpsrv_common/fs_data/CMakeLists.txt")
include("${SdkRootDirPath}/examples/lwip_examples/httpsrv_common/fs_data/webpage/CMakeLists.txt")

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 14)
    set_source_files_properties(
        "${SdkRootDirPath}/middleware/lwip/src/apps/httpsrv/httpsrv_sha1.c"
        PROPERTIES COMPILE_OPTIONS "-Wno-stringop-overread"
    )
endif()

mcux_add_macro(
    CC "-DUSB_STACK_BM -DPRINTF_ADVANCED_ENABLE=1"
)
mcux_add_macro(
    TOOLCHAINS armgcc
    CC "-D_POSIX_SOURCE"
)
mcux_add_source(
    SOURCES
        src/main.cpp
)
mcux_add_include(
    BASE_PATH "${SdkRootDirPath}"
    INCLUDES "examples/_boards/${board}"
)

mcux_add_linker_symbol(
    SYMBOLS "__stack_size__=2048\
             __heap_size__=60000"
)

mcux_convert_binary(
    BINARY "${APPLICATION_BINARY_DIR}/${MCUX_SDK_PROJECT_NAME}.hex"
)
