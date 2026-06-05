cmake_minimum_required(VERSION 3.28)

# Load pico_sdk
set(PICO_BOARD "pico2_w" CACHE STRING "Pico target board for this project")
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

# Must be defined before the call to pico_sdk_init()
project(picorccar C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

pico_sdk_init()

if (NOT DEFINED LOGGING_THRESHOLD)
    set(LOGGING_THRESHOLD ULOG_INFO_LEVEL)
endif()

# -Wall and -Wextra for subprojects
add_library(picorccar_warnings INTERFACE)
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(picorccar_warnings
        INTERFACE
            -Wall
            -Wextra
    )
endif()

# Common headers
add_library(picorccar_firmware_headers INTERFACE)
target_include_directories(picorccar_firmware_headers
    INTERFACE
        ${CMAKE_SOURCE_DIR}/firmware/include
)

if (NOT DEFINED PICO_MOUNT_PATH)
    if (DEFINED ENV{USER})
        set(PICO_MOUNT_PATH "/media/$ENV{USER}/RP2350" CACHE PATH "Pico USB mass-storage mount path")
    endif()
endif()
if (DEFINED PICO_MOUNT_PATH)
    set(CMAKE_INSTALL_PREFIX "${PICO_MOUNT_PATH}" CACHE PATH "Install prefix for Pico UF2" FORCE)
endif()

add_subdirectory(firmware/ble)
add_subdirectory(firmware/car)
add_subdirectory(firmware/controller)
