#include "esp_err.h"
#include "nvs_flash.h"
#include "host/ble_hs.h" // ble_hs_adv_parse_fields
#include "host/util/util.h" // ble_hs_util_ensure_addr
#include "nimble/nimble_port.h" // nimble_port_run
#include "nimble/nimble_port_freertos.h" // nimble_port_freertos_init

// The BLE 16-bit UUID for BTHome (bthome.io)
#define BLE_UUID16_BTHOME 0xFCD2

#define BTHOME_PACKET_ID    0x00
#define BTHOME_BATTERY      0x01
#define BTHOME_ILLUMINANCE  0x05
#define BTHOME_MOTION       0x21
#define BTHOME_WINDOW       0x2D
#define BTHOME_BUTTON       0x3A
#define BTHOME_ROTATION     0x3F

void bthome_init(void);