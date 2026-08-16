
/*
 * Simple example of analysing BLE GAP messages from BTHome (bthome.io) devices.
 *
 * Use NimBLE and disable Bluetooth 5.0 as the devices are 4.2:
 * - CONFIG_BT_ENABLED=y
 * - CONFIG_BT_NIMBLE_ENABLED=y
 * - CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT=n
 * 
 * BTHome is always starts with 0x02, 0x01 & 0x06:
 * - 0x02: 2 bytes
 * - 0x01: Flags
 * - 0x06: Flag Data:
 *   - 0x04: BR/EDR Not Supported
 *   - 0x02: LE General Discoverable Mode
 * 
 * Then Service Data with a 16-bit UUID:
 * - 0x0E: 14 bytes
 * - 0x16: Service Data with a 16-bit UUID
 * - 0xD2FC: 0xFCD2 - BTHome UUID
 * - 0x44:
 *     - bit0: Encryption On
 *     - bit2: Device sends upon a trigger
 *     - bit5-7: BTHome version 0x2 = v1, 0x4 = v2
 * 
 * Example:
 *   Adv report from AA:BB:CC:DD:EE:FF
 *   Raw service data: D2 FC 44 00 0B 01 64 05 34 49 0A 2D 01 3F 00 00 
 *   PacketID=11, Battery%=100, Illuminance=6741.00, Door=1, Rotation=0.0, 
 */
 
#include "bthome.h"

void nimble_host_task(void *param)
{
    // This runs until nimble_port_stop() is issued 
    nimble_port_run();

    // If stopped, deinitialise
    nimble_port_freertos_deinit();
}

// Callback for startup or reset of stack to ensure there is a device
static void nimble_sync_callback(void) {
    // Ensure there is one Bluetooth device
    int ret = ble_hs_util_ensure_addr(0);
    assert(ret == 0);
}

static void bthome_parse(ble_addr_t addr, const uint8_t *svc_data, uint8_t svc_data_len) {
    printf("Adv report from %02X:%02X:%02X:%02X:%02X:%02X\n",
        addr.val[5], addr.val[4], addr.val[3],
        addr.val[2], addr.val[1], addr.val[0]
    );

    printf("Raw service data: ");
    for (int i = 0; i < svc_data_len; i++) {
        printf("%02X ", svc_data[i]);
    }
    printf("\n");

    // Is it BTHome v2 (0x40 >> 5)?
    // https://bthome.io/format/
    if ((svc_data[2] >> 5) == 0x2) {
        for (int i = 3; i < svc_data_len; i++) {
            switch (svc_data[i++]) {
                
            case 0x00: // Packet ID (counter)
                printf("PacketID=%d, ", svc_data[i]);
            break;

            case 0x01: // Battery (percentage)
                printf("Battery%%=%d, ", svc_data[i]);
            break;

            case 0x05: // Illuminance (lux)
                // 0x138A14 - 13460.67 lx
                printf("Illuminance=%0.2f, ", (float)(((uint32_t)svc_data[i]) | ((uint32_t)svc_data[i+1] << 8) | ((uint32_t)svc_data[i+2] << 16)) * 0.01);
                i += 2;
            break;

            case 0x21: // IR Motion
                printf("IR Motion=%d, ", svc_data[i]);
            break;

            case 0x2D: // Door: Open/Close
                printf("Door=%d, ", svc_data[i]);
            break;

            case 0x3F: // Rotation (degrees)
                // 0x020C = 307.4 degrees
                printf("Rotation=%0.1f, ", (float)(((uint16_t)svc_data[i]) | ((uint16_t)svc_data[i+1] << 8)) * 0.1);
                i++;
            break;

            case 0x3A: // Button (press))
                printf("Button=%d, ", svc_data[i]);
            break;

            default:
                // Maybe do something here? 
            break;
            }
        }
        printf("\n");
    }
}

static int bthome_gap_event_callback(struct ble_gap_event *event, void *arg) {
    if (event->type == BLE_GAP_EVENT_DISC) {
        struct ble_gap_disc_desc *ar = &event->disc;
        struct ble_hs_adv_fields adv_fields;

        if (ble_hs_adv_parse_fields(&adv_fields, event->disc.data, event->disc.length_data) != 0) {
            return 0;
        }

        // Look for a Service Data UUID 16-bit message with flag of 0x06 (see main comment)
        if (adv_fields.flags == 0x06 && 
            adv_fields.svc_data_uuid16 != NULL && 
            adv_fields.svc_data_uuid16_len) {
            // UUID is in first two bytes, little endian
            uint16_t uuid16 = adv_fields.svc_data_uuid16[0] |
                            ((uint16_t)adv_fields.svc_data_uuid16[1] << 8);
            // Check to see if it is BTHOME
            if (uuid16 == BLE_UUID16_BTHOME) {
                bthome_parse(ar->addr, adv_fields.svc_data_uuid16, adv_fields.svc_data_uuid16_len);
            }
        }
    }

    return 0;
}

void bthome_discover(void) {
    // Configuration for scanning for GAP messages
    struct ble_gap_disc_params disc_params = { 
        .filter_duplicates = 1, // BTHome repeats, so filter out duplicates
        .passive = 1 // Scan passively
    };

    // Scan passively (so no address needed) and forever 
    int ret = ble_gap_disc(0, BLE_HS_FOREVER, &disc_params, bthome_gap_event_callback, NULL);
    if (ret != 0) { return; }
}

void bthome_init(void) {    
    // For PHY calibration data
    esp_err_t ret = nvs_flash_init();
    if  (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialise BLE host stack
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        printf("Could not initialise NimBLE: %d\n", ret);
        return;
    }

    // This is not a professional solution, so use FIFO with bond details
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    // What do to at startup or after a reset
    ble_hs_cfg.sync_cb = nimble_sync_callback;

    // Start NimBLE host stack in FreeRTOS task
    nimble_port_freertos_init(nimble_host_task);

    // Start custom BTHome scanning
    bthome_discover();
}