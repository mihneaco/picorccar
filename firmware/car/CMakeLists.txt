project(picorccar_car NONE)

add_executable(${PROJECT_NAME}
    src/motor_driver.cpp
    src/main.cpp
    src/remote_link.cpp
    ${CMAKE_SOURCE_DIR}/third_party/ulog/ulog.c
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        ULOG_ENABLED
        ULOG_MAX_MESSAGE_LENGTH=1024
        LOGGING_THRESHOLD=${LOGGING_THRESHOLD}
)

# This firmware uses AP mode without a DHCP server. Pin the AP IPv4 settings
# explicitly so controller firmware can use a matching static client address.
pico_configure_ip4_address(${PROJECT_NAME} PRIVATE CYW43_DEFAULT_IP_AP_ADDRESS "192.168.4.1")
pico_configure_ip4_address(${PROJECT_NAME} PRIVATE CYW43_DEFAULT_IP_AP_GATEWAY "192.168.4.1")
pico_configure_ip4_address(${PROJECT_NAME} PRIVATE CYW43_DEFAULT_IP_MASK "255.255.255.0")

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${CMAKE_SOURCE_DIR}/third_party/ulog
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        picorccar_firmware_headers
        picorccar_warnings
        pico_stdlib
        pico_cyw43_arch_lwip_threadsafe_background
        hardware_pwm
        hardware_gpio
)

## STDIO over usb ##
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

## Generate the .uf2 file ##
pico_add_extra_outputs(${PROJECT_NAME})

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.uf2"
        DESTINATION .
        RENAME "${PROJECT_NAME}.uf2"
        COMPONENT car)
