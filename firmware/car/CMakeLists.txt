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
        PICORCCAR_BLE_PERIPHERAL=1
)

pico_btstack_make_gatt_header(
    ${PROJECT_NAME}
    PRIVATE
    ${PICORCCAR_BLE_GATT_FILE}
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${CMAKE_SOURCE_DIR}/third_party/ulog
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        picorccar_firmware_headers
        picorccar_ble_headers
        picorccar_warnings
        pico_stdlib
        pico_cyw43_arch_none
        pico_btstack_ble
        pico_btstack_cyw43
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
