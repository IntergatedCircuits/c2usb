# make mcux_config.h available for c2usb
target_include_directories(c2usb PUBLIC ${CMAKE_BINARY_DIR})

if(CONFIG_C2USB_MCUX_MAC)
    # add these sources to the firmware executable target
    target_sources(${MCUX_SDK_PROJECT_NAME}
        PRIVATE
            mcux_mac.cpp
    )
    if(NOT DEFINED CONFIG_C2USB_MCUX_USB_COEXISTENCE)
        # since MCUX_COMPONENT_middleware.usb.device.controller.driver cannot be disabled
        mcux_project_remove_source(BASE_PATH ${SdkRootDirPath}/middleware/usb/device
            SOURCES usb_device_dci.c
        )
    endif()
endif()
