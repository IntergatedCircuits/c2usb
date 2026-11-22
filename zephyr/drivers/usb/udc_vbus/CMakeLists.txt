if(CONFIG_C2USB_UDC_VBUS)
    zephyr_library()
    zephyr_library_sources(udc_vbus.c)
    # udc_submit_event() is only declared in the private header of the UDC drivers
    zephyr_library_include_directories(${ZEPHYR_BASE}/drivers/usb/udc)
endif()
