project(picorccar_controller NONE)

add_executable(${PROJECT_NAME}
    src/main.cpp
    ${CMAKE_SOURCE_DIR}/third_party/ulog/ulog.c
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        ULOG_ENABLED
        ULOG_MAX_MESSAGE_LENGTH=1024
        LOGGING_THRESHOLD=${LOGGING_THRESHOLD}
)

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
)

pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

pico_add_extra_outputs(${PROJECT_NAME})

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.uf2"
        DESTINATION .
        RENAME "${PROJECT_NAME}.uf2"
        COMPONENT controller)
