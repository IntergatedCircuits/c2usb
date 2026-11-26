if(CONFIG_C2USB)
    # hid-rp and etl are west modules, built by their own zephyr CMakeLists.txt
    set(C2USB_SDK_PROVIDED_DEPENDENCIES hid-rp etl)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../c2usb c2usb)

    target_link_libraries(app PRIVATE c2usb)
    target_link_libraries(c2usb
        PUBLIC      zephyr_interface
        INTERFACE   kernel
    )
    # prevent build errors from late generated zephyr/heap_constants.h
    add_dependencies(c2usb zephyr_generated_headers)

    add_subdirectory(../c2usb/bluetooth c2usb/bluetooth)
    add_subdirectory(../c2usb/usb/df/vendor/zephyr c2usb/usb/df/vendor/zephyr)
    add_subdirectory(../c2usb/i2c/vendor/stm32 c2usb/i2c/vendor/stm32)
    add_subdirectory(drivers/usb/udc_vbus)

    zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR})

    # set the standard compiler flag based on zephyr Kconfig, so e.g. assert() works as intended,
    # after all modules and the application have added their compiler flags
    function(c2usb_zephyr_set_ndebug)
        get_target_property(definitions zephyr_interface INTERFACE_COMPILE_DEFINITIONS)
        get_target_property(options zephyr_interface INTERFACE_COMPILE_OPTIONS)
        string(REGEX MATCH "(^|[^A-Za-z0-9_]|-D)NDEBUG([^A-Za-z0-9_]|$)" ndebug_defined
            "${definitions};${options};${CMAKE_C_FLAGS};${CMAKE_CXX_FLAGS}")
        if(CONFIG_ASSERT AND ndebug_defined)
            message(FATAL_ERROR "NDEBUG is defined while CONFIG_ASSERT is enabled, disabling assert()")
        elseif(NOT CONFIG_ASSERT AND NOT ndebug_defined)
            target_compile_definitions(zephyr_interface INTERFACE NDEBUG)
        endif()
    endfunction()
    cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL c2usb_zephyr_set_ndebug)
endif()
