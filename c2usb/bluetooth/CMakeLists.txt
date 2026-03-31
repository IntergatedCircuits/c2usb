target_sources_ifdef(CONFIG_C2USB_HOGP c2usb
    PRIVATE
        hid_over_gatt.cpp
)
