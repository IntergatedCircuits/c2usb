# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fpermissive" PARENT_SCOPE) # https://github.com/zephyrproject-rtos/zephyr/pull/69490

target_sources_ifdef(CONFIG_C2USB_UDC_MAC c2usb
    PRIVATE
        udc_mac.cpp
)
target_sources_ifdef(CONFIG_SHELL_BACKEND_C2USB c2usb
    PRIVATE
        shell.c
        shell.cpp
)
