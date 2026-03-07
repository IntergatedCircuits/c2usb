target_link_libraries(c2usb
    PUBLIC
        zephyr_interface
    INTERFACE
        kernel
)
target_sources_ifdef(CONFIG_C2USB_UDC_MAC c2usb
    PRIVATE
        udc_mac.cpp
)
target_sources_ifdef(CONFIG_C2USB_BLUETOOTH c2usb
    PRIVATE
        bluetooth/hid.cpp
)
target_sources_ifdef(CONFIG_SHELL_BACKEND_C2USB c2usb
    PRIVATE
        usb_shell.c
        usb_shell.cpp
)
