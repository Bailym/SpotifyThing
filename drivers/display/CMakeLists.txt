# SSH1106 OLED Display Driver

add_library(ssh1106 INTERFACE)

target_sources(ssh1106 INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/ssh1106.c
)

target_include_directories(ssh1106 INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(ssh1106 INTERFACE
    pico_stdlib
    hardware_i2c
    hardware_gpio
)
