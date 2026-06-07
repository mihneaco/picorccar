project(picorccar_ble NONE)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/generate_uuid.cmake)

set(PICORCCAR_GENERATED_FIRMWARE_DIR ${CMAKE_BINARY_DIR}/generated/firmware)
set(PICORCCAR_GENERATED_BLE_DIR ${PICORCCAR_GENERATED_FIRMWARE_DIR}/ble)
set(PICORCCAR_BLE_GATT_FILE ${PICORCCAR_GENERATED_BLE_DIR}/car_gatt.gatt)

set(PICORCCAR_BLE_DEVICE_NAME "PicoCar" CACHE STRING "BLE device name advertised by the car firmware")
set_generated_uuid(PICORCCAR_BLE_SERVICE_UUID "Generated 128-bit BLE command service UUID")
set_generated_uuid(PICORCCAR_BLE_COMMAND_UUID "Generated 128-bit BLE command characteristic UUID")
uuid_to_c_bytes("${PICORCCAR_BLE_SERVICE_UUID}" PICORCCAR_BLE_SERVICE_UUID_BYTES)
uuid_to_c_bytes("${PICORCCAR_BLE_COMMAND_UUID}" PICORCCAR_BLE_COMMAND_UUID_BYTES)

configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/include/ble_conf.h.in
    ${PICORCCAR_GENERATED_BLE_DIR}/ble_conf.h
    @ONLY
)
configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/car_gatt.gatt.in
    ${PICORCCAR_BLE_GATT_FILE}
    @ONLY
)

add_library(picorccar_ble_headers INTERFACE)
target_include_directories(picorccar_ble_headers
    INTERFACE
        ${PICORCCAR_GENERATED_FIRMWARE_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/include
)

set(PICORCCAR_BLE_GATT_FILE ${PICORCCAR_BLE_GATT_FILE} PARENT_SCOPE)
