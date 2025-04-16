
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
        gatt.hpp
        hid.hpp
        le.hpp
    PRIVATE
        hid.cpp
)
