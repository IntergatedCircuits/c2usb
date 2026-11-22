if(CONFIG_C2USB)
    set(C2USB_SDK_PROVIDED_DEPENDENCIES hid-rp)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../c2usb c2usb)

    target_link_libraries(${MCUX_SDK_PROJECT_NAME} PRIVATE c2usb)
    add_subdirectory(../c2usb/usb/df/vendor/lwip c2usb/usb/df/vendor/lwip)
    add_subdirectory(../c2usb/usb/df/vendor/nxp c2usb/usb/df/vendor/nxp)
endif()
