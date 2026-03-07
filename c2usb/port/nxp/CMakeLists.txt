if(CONFIG_C2USB_MCUX_MAC)
    # add these sources to the firmware executable target
    target_sources(${MCUX_SDK_PROJECT_NAME}
        PRIVATE
            dwc3_mac.cpp
            ehci_mac.cpp
            khci_mac.cpp
            lpcip3511_mac.cpp
            mcux_mac.cpp
    )
    target_link_libraries(${MCUX_SDK_PROJECT_NAME}
        PRIVATE
            c2usb
    )
    # since MCUX_COMPONENT_middleware.usb.device.controller.driver cannot be disabled
    mcux_project_remove_source(BASE_PATH ${SdkRootDirPath}/middleware/usb/device
        SOURCES usb_device_dci.c
    )
else()
    set_target_properties(c2usb PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
