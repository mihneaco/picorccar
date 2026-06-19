project(picorccar_car NONE)

add_executable(${PROJECT_NAME}
    src/motor_driver.cpp
    src/main.cpp
    src/command_receiver.cpp
    ${CMAKE_SOURCE_DIR}/third_party/ulog/ulog.c
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        PICORCCAR_ACCESS_POINT_SSID="${PICORCCAR_WIFI_AP_SSID}"
        PICORCCAR_ACCESS_POINT_PSK="${PICORCCAR_WIFI_AP_PSK}"
        PICORCCAR_UDP_SERVER_PORT=${PICORCCAR_UDP_SERVER_PORT}
)

# This firmware uses AP mode without a DHCP server. Pin the AP IPv4 settings
# explicitly so controller firmware can use a matching static client address.
pico_configure_ip4_address(${PROJECT_NAME} PRIVATE CYW43_DEFAULT_IP_AP_ADDRESS "${PICORCCAR_WIFI_AP_IP}")
pico_configure_ip4_address(${PROJECT_NAME} PRIVATE CYW43_DEFAULT_IP_AP_GATEWAY "${PICORCCAR_WIFI_AP_IP}")
pico_configure_ip4_address(${PROJECT_NAME} PRIVATE CYW43_DEFAULT_IP_MASK "${PICORCCAR_WIFI_NETMASK}")

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
