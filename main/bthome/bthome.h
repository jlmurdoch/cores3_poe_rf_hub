#include "esp_err.h"
#include "nvs_flash.h"
#include "host/ble_hs.h" // ble_hs_adv_parse_fields
#include "host/util/util.h" // ble_hs_util_ensure_addr
#include "nimble/nimble_port.h" // nimble_port_run
#include "nimble/nimble_port_freertos.h" // nimble_port_freertos_init

// The BLE 16-bit UUID for BTHome (bthome.io)
#define BLE_UUID16_BTHOME 0xFCD2

void bthome_init(void);