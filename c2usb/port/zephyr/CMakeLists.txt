
target_sources_ifdef(C2USB_CPP ${PROJECT_NAME}
    PUBLIC
        message_queue.hpp
        tick_timer.hpp
        semaphore.hpp
)
target_sources_ifdef(CONFIG_C2USB_UDC_MAC ${PROJECT_NAME}
    PUBLIC
        udc_mac.hpp
    PRIVATE
        udc_mac.cpp
)
target_sources_ifdef(CONFIG_C2USB_BLUETOOTH ${PROJECT_NAME}
    PUBLIC
        bluetooth/gatt.hpp
        bluetooth/hid.hpp
        bluetooth/le.hpp
    PRIVATE
        bluetooth/hid.cpp
)
target_sources_ifdef(CONFIG_SHELL_BACKEND_C2USB ${PROJECT_NAME}
    PUBLIC
        usb_shell.hpp
    PRIVATE
        usb_shell.c
        usb_shell.cpp
)
