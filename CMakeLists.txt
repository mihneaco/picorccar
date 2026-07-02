cmake_minimum_required(VERSION 3.28)

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

project(picorccar C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

pico_sdk_init()

if (NOT DEFINED LOGGING_THRESHOLD)
    set(LOGGING_THRESHOLD ULOG_INFO_LEVEL)
endif()

set(PICORCCAR_COMMON_COMPILE_DEFINITIONS
    ULOG_ENABLED
    ULOG_MAX_MESSAGE_LENGTH=1024
    LOGGING_THRESHOLD=${LOGGING_THRESHOLD}
    # Cap how long a stalled USB serial host can block a firmware's main loop.
    # SDK default is 500000us/write, which snowballs into multi-second core
    # stalls when the monitor stops draining. 1ms bounds worst-case loop jitter
    # and only drops log output under sustained backpressure. Applies to both
    # firmwares since both are stdio-USB-only.
    PICO_STDIO_USB_STDOUT_TIMEOUT_US=1000
)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(APPEND PICORCCAR_COMMON_COMPILE_DEFINITIONS
        PICORCCAR_DEBUG
    )
endif()

add_compile_definitions(${PICORCCAR_COMMON_COMPILE_DEFINITIONS})

set(PICORCCAR_WIFI_RANDOM_ALPHABET "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")
set(PICORCCAR_WIFI_AP_SSID_LENGTH 16)
set(PICORCCAR_WIFI_AP_PSK_LENGTH 32)
if (NOT DEFINED PICORCCAR_WIFI_AP_SSID OR PICORCCAR_WIFI_AP_SSID STREQUAL "")
    string(RANDOM
           LENGTH ${PICORCCAR_WIFI_AP_SSID_LENGTH}
           ALPHABET "${PICORCCAR_WIFI_RANDOM_ALPHABET}"
           generated_picorccar_wifi_ap_ssid)
    set(PICORCCAR_WIFI_AP_SSID
        "${generated_picorccar_wifi_ap_ssid}"
        CACHE STRING
        "Generated access point SSID shared by the car and controller firmware"
        FORCE)
endif()
if (NOT DEFINED PICORCCAR_WIFI_AP_PSK OR PICORCCAR_WIFI_AP_PSK STREQUAL "")
    string(RANDOM
           LENGTH ${PICORCCAR_WIFI_AP_PSK_LENGTH}
           ALPHABET "${PICORCCAR_WIFI_RANDOM_ALPHABET}"
           generated_picorccar_wifi_ap_psk)
    set(PICORCCAR_WIFI_AP_PSK
        "${generated_picorccar_wifi_ap_psk}"
        CACHE STRING
        "Generated WPA2 PSK shared by the car and controller firmware"
        FORCE)
endif()
set(PICORCCAR_WIFI_AP_IP      "192.168.4.1"   CACHE STRING "Static IPv4 address used by the car access point")
set(PICORCCAR_WIFI_STA_IP     "192.168.4.2"   CACHE STRING "Static IPv4 address used by the controller station")
set(PICORCCAR_WIFI_NETMASK    "255.255.255.0" CACHE STRING "Static IPv4 netmask used by both firmwares")
set(PICORCCAR_UDP_SERVER_PORT "12345"         CACHE STRING "UDP port used by the car command receiver")

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
        ${CMAKE_SOURCE_DIR}/include
)

if (NOT DEFINED PICO_MOUNT_PATH)
    if (DEFINED ENV{USER})
        set(PICO_MOUNT_PATH "/media/$ENV{USER}/RP2350" CACHE PATH "Pico USB mass-storage mount path")
    endif()
endif()
if (DEFINED PICO_MOUNT_PATH)
    set(CMAKE_INSTALL_PREFIX "${PICO_MOUNT_PATH}" CACHE PATH "Install prefix for Pico UF2" FORCE)
endif()

add_subdirectory(car)
add_subdirectory(controller)
