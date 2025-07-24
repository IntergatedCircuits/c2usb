include(CPM)
CPMAddPackage("gh:catchorg/Catch2@3.5.4")
list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)

include(CTest)
include(Catch)

add_executable(${PROJECT_NAME}-test)
target_sources(${PROJECT_NAME}-test
    PRIVATE
        df/config.test.cpp
)
target_link_libraries(${PROJECT_NAME}-test
    PRIVATE
        Catch2::Catch2WithMain
        ${PROJECT_NAME}
        ${PROJECT_NAME}-example-hid
)
catch_discover_tests(${PROJECT_NAME}-test)

target_compile_options(${PROJECT_NAME}-test
    PRIVATE
        # https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html
        # https://embeddedartistry.com/blog/2023/09/20/leveraging-your-toolchain-to-improve-security/
        -Wall
        -Wextra
        -Werror
        -Wsign-conversion
        $<$<CXX_COMPILER_ID:GNU>:
            -fconcepts-diagnostics-depth=20
            -Wshift-overflow=2
            -Wdangling-reference
            -Woverloaded-virtual=2
        >
)
