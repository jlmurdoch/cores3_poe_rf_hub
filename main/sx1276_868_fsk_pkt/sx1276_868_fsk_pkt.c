#include "sx1276_868_fsk_pkt/sx1276_868_fsk_pkt.h"


void sx1276_868_task(void *pvParameters) {
    spi_device_handle_t spi = (spi_device_handle_t)pvParameters;

    for (;;) {
        // Wait for ISR call
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        printf("[SX127x] 868MHz RSSI Detected\n");
        uint8_t value = sx127x_read_single(spi, 0x00);
        printf("[SX127x] 868MHz Packet Size: %d bytes\n", value);
        printf("[SX127x] 868MHz Packet Data:");
        uint8_t y = 0;
        for (int x = 0; x < value; x++) {
            y = sx127x_read_single(spi, 0x00);
            printf(" %02x", y);
        }
        printf("\n");
    }
}

/**
 * @brief Interrupt for when RSSI goes high on DIO2
 * @param arg Unused
 */
static void IRAM_ATTR sx1276_868_task_isr(void *arg) {

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    vTaskNotifyGiveFromISR(sx1276_868_task_handle, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void init_sx1276_868_fsk_pkt(void) {
    esp_err_t err;

    spi_device_handle_t sx1276_868_spi;
    spi_device_interface_config_t sx1276_868_cfg = {
        .command_bits = 8,
        .clock_speed_hz = SPI_MASTER_FREQ_10M,
        .mode = 0,
        .spics_io_num = PIN_NUM_868_CS,
        .queue_size = 1,
    };
    err = spi_bus_add_device(SPI_HOST_ID, &sx1276_868_cfg, &sx1276_868_spi);

    gpio_set_direction(PIN_NUM_868_RST, GPIO_MODE_OUTPUT);

    // SX1276 868MHz RSSI GPIO - ISR for signal detection
    gpio_set_direction(PIN_NUM_868_DIO0, GPIO_MODE_INPUT);
    gpio_set_intr_type(PIN_NUM_868_DIO0, GPIO_INTR_POSEDGE);
    gpio_pulldown_dis(PIN_NUM_868_DIO0);
    gpio_pullup_dis(PIN_NUM_868_DIO0);

    sx127x_init_fskpacket(sx1276_868_spi, PIN_NUM_868_RST, 868299000, 9600);

    // Start 868 post-processor task
    xTaskCreate(sx1276_868_task, "868MHz Task", 4096, (void *)sx1276_868_spi, 10, &sx1276_868_task_handle);

    // Attach the interrupt service routine, dio0_rssi_isr(), to DIO0
    err = gpio_isr_handler_add(PIN_NUM_868_DIO0, sx1276_868_task_isr, NULL);
    ESP_ERROR_CHECK(err);
}