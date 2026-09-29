# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fpermissive" PARENT_SCOPE) # https://github.com/zephyrproject-rtos/zephyr/pull/69490

target_sources_ifdef(CONFIG_C2USB_UDC_MAC c2usb
    PRIVATE
        udc_mac.cpp
)
target_sources_ifdef(CONFIG_C2USB_SHELL_BACKEND c2usb
    PRIVATE
        shell.c
        shell.cpp
)
if(CONFIG_C2USB_SHELL_MCUMGR_TRANSPORT)
    target_include_directories(c2usb PRIVATE
        ${ZEPHYR_BASE}/subsys/mgmt/mcumgr/transport/include
    )
endif()
target_sources_ifdef(CONFIG_C2USB_CDC_NCM_ETHERNET c2usb
    PRIVATE
        cdc_ncm_ethernet.c
        cdc_ncm_ethernet.cpp
)
