if(CONFIG_C2USB_LWIP_NCM_NETIF)
    if(MCUX_SDK_PROJECT_NAME)
        target_sources(${MCUX_SDK_PROJECT_NAME}
            PRIVATE
                ncm_netif.cpp
        )
    endif()
endif()
