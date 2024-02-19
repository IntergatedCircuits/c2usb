add_library(${PROJECT_NAME}-example-hid)
target_sources(${PROJECT_NAME}-example-hid
    PUBLIC
        leds_saving_keyboard.hpp
)
target_include_directories(${PROJECT_NAME}-example-hid
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(${PROJECT_NAME}-example-hid PUBLIC
    ${PROJECT_NAME}
)
