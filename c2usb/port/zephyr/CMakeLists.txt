target_sources(${PROJECT_NAME}
    PUBLIC
        udc_mac.hpp
        message_queue.hpp
        tick_timer.hpp
        semaphore.hpp
    PRIVATE
        udc_mac.cpp
)
