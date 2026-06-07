project(picorccar_ble NONE)

set(PICORCCAR_GENERATED_FIRMWARE_DIR ${CMAKE_BINARY_DIR}/generated/firmware)
set(PICORCCAR_GENERATED_BLE_DIR ${PICORCCAR_GENERATED_FIRMWARE_DIR}/ble)
set(PICORCCAR_BLE_GATT_FILE ${PICORCCAR_GENERATED_BLE_DIR}/car_gatt.gatt)
set(PICORCCAR_BLE_DEVICE_NAME "PicoCar" CACHE STRING "BLE device name advertised by the car firmware")
set(PICORCCAR_BLE_SERVICE_UUID "a0c4997e-aafa-4edb-bb6b-07d45cad8318")
set(PICORCCAR_BLE_SERVICE_UUID_BYTES
    "0xa0, 0xc4, 0x99, 0x7e, 0xaa, 0xfa, 0x4e, 0xdb, 0xbb, 0x6b, 0x07, 0xd4, 0x5c, 0xad, 0x83, 0x18")
set(PICORCCAR_BLE_COMMAND_UUID "e70a94ca-4ee5-4644-9706-37f4670df528")
set(PICORCCAR_BLE_COMMAND_UUID_BYTES
    "0xe7, 0x0a, 0x94, 0xca, 0x4e, 0xe5, 0x46, 0x44, 0x97, 0x06, 0x37, 0xf4, 0x67, 0x0d, 0xf5, 0x28")

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
