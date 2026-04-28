cmake_minimum_required(VERSION 3.28)

if (NOT DEFINED PICOTOOL_FETCH_FROM_GIT_PATH)
    if (DEFINED ENV{HOME})
        set(PICOTOOL_FETCH_FROM_GIT_PATH "$ENV{HOME}/pico-tool" CACHE PATH "Local picotool checkout path")
    endif()
endif()

if (NOT DEFINED PICO_SDK_PATH)
    if (DEFINED ENV{HOME})
        set(PICO_SDK_PATH "$ENV{HOME}/pico-sdk" CACHE PATH "Local pico-sdk checkout path")
    endif()
endif()

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

set(MIN_PICO_SDK_VERSION "2.2.0")
if (PICO_SDK_VERSION_STRING VERSION_LESS MIN_PICO_SDK_VERSION)
    message(FATAL_ERROR
        "Unsupported Pico SDK version '${PICO_SDK_VERSION_STRING}' at '${PICO_SDK_PATH}'. "
        "Expected >= '${MIN_PICO_SDK_VERSION}'.")
endif()

project(picorccar C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

pico_sdk_init()

add_executable(${PROJECT_NAME}
    third_party/ulog/ulog.c
    src/motor_driver.cpp
    src/main.cpp
    src/remote_link.cpp
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        ULOG_ENABLED
        ULOG_MAX_MESSAGE_LENGTH=1024
)

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PROJECT_NAME}
        PRIVATE
            -Wall
            -Wextra
    )
endif()

target_include_directories(${PROJECT_NAME}
    PRIVATE
        include
        third_party/ulog
)

target_link_libraries(${PROJECT_NAME}
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
    hardware_pwm
    hardware_gpio
)

## STDIO over usb ##
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

# Install Target #

## Generate the .uf2 file ##
pico_add_extra_outputs(${PROJECT_NAME})
set(PICO_UF2_FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.uf2")

## Install the .uf2 file to PICO_MOUNT_PATH ##
if (NOT DEFINED PICO_MOUNT_PATH)
    if (DEFINED ENV{USER})
        set(PICO_MOUNT_PATH "/media/$ENV{USER}/RP2350" CACHE PATH "Pico USB mass-storage mount path")
    endif()
endif()
if (DEFINED PICO_MOUNT_PATH)
    set(CMAKE_INSTALL_PREFIX "${PICO_MOUNT_PATH}" CACHE PATH "Install prefix for Pico UF2" FORCE)
endif()
install(FILES "${PICO_UF2_FILE}" DESTINATION . RENAME "${PROJECT_NAME}.uf2")
