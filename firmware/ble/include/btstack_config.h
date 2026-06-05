/*
    based on, with minor edits:
    https://github.com/raspberrypi/pico-examples/blob/master/pico_w/bt/standalone/btstack_config_common.h
*/

#pragma once

#if defined(PICORCCAR_BLE_PERIPHERAL)
#define ENABLE_LE_PERIPHERAL
#endif

#if defined(PICORCCAR_BLE_CENTRAL)
#define ENABLE_LE_CENTRAL
// Central also needs this because BTstack/Pico SDK shared LE HCI code references peripheral advertising state.
#define ENABLE_LE_PERIPHERAL
#define MAX_NR_GATT_CLIENTS 1
#endif

#ifndef NDEBUG
#define ENABLE_LOG_ERROR
#endif
// Required by pico_btstack_base/hci_dump_embedded_stdout.c even if HCI dump is unused.
#define ENABLE_PRINTF_HEXDUMP

// BTstack configuration. buffers, sizes, ...
#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_PAYLOAD_SIZE (255 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4
#define MAX_NR_HCI_CONNECTIONS 1
#define MAX_NR_SM_LOOKUP_ENTRIES 1
#define MAX_NR_WHITELIST_ENTRIES 1
#define MAX_NR_LE_DEVICE_DB_ENTRIES 2

// Limit number of ACL/SCO Buffer to use by stack to avoid cyw43 shared bus overrun
#define MAX_NR_CONTROLLER_ACL_BUFFERS 3
#define MAX_NR_CONTROLLER_SCO_PACKETS 3

// Enable and configure HCI Controller to Host Flow Control to avoid cyw43 shared bus overrun
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define HCI_HOST_ACL_PACKET_LEN (255 + 4)
#define HCI_HOST_ACL_PACKET_NUM 3
#define HCI_HOST_SCO_PACKET_LEN 120
#define HCI_HOST_SCO_PACKET_NUM 3

// 1 is minimum, we use 2 as headroom for 1 stale peer
#define NVM_NUM_DEVICE_DB_ENTRIES 2

// We don't give btstack a malloc, so use a fixed-size ATT DB.
#define MAX_ATT_DB_SIZE 256

// BTstack HAL configuration
#define HAVE_EMBEDDED_TIME_MS
// map btstack_assert onto Pico SDK assert()
#define HAVE_ASSERT
// Some USB dongles take longer to respond to HCI reset (e.g. BCM20702A).
#define HCI_RESET_RESEND_TIMEOUT_MS 1000
#define ENABLE_SOFTWARE_AES128
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS
