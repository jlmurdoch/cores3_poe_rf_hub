#include "sx1278_433_ook_raw/sx1278_433_ook_raw.h"

/**
 * @brief Interrupt for when RSSI goes high on DIO2
 * @param arg Unused
 */
static void IRAM_ATTR sx1278_433_ulp_isr(void *arg) {
    esp_err_t err;
    err = ulp_run(&ulp_entry - RTC_SLOW_MEM);
    ESP_ERROR_CHECK(err);
}

void sx1278_433_task(void *pvParameters) {
    spi_device_handle_t spi = (spi_device_handle_t)pvParameters;

    for (;;) {
        // Wait for ISR call
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // RXDone
        if (ulp_state == 1) {
            esp_err_t err;
            // Print the RFM ULP variables
            printf("[SX127x] 433MHz Raw Data: %lld - 0x%04x,0x%04x\n", 
                esp_timer_get_time() / 1000000,
                (uint16_t)(&ulp_value)[0], (uint16_t)(&ulp_value)[1]);

            int64_t sensor_id = ((uint16_t)(&ulp_value)[0] >> 8) & 0xFF;
            uint8_t battery = ((uint16_t)(&ulp_value)[0] >> 7) & 0x1; 
            uint8_t manual = ((uint16_t)(&ulp_value)[0] >> 6) & 0x1;
            uint8_t channel = (((uint16_t)(&ulp_value)[0]>> 4) & 0x3) + 1;
            double temp = (float)((((((uint16_t)(&ulp_value)[0] & 0x7) << 8) | (((uint16_t)(&ulp_value)[1] >> 8) & 0xFF))
                            ^ (((uint16_t)(&ulp_value)[0] & 0x8) >> 3)) + (((uint16_t)(&ulp_value)[0] & 0x8) >> 3)) * 0.1;
            int64_t humidity = ((uint16_t)(&ulp_value)[1] & 0xFF);
            printf("[SX127x] 433MHz Parsed Data: Id: 0x%02llx, Bat: %d, Man: %d, Ch: %d, Temp: %.1f°C, RH: %lld%%\n", 
                sensor_id, battery, manual, channel, temp, humidity);

            char *pb_data = (char *)malloc(sizeof(char) * 1024);
            size_t pb_len = otlp_climate(pb_data, sensor_id, temp, humidity);
            otlp_nanopb_print(pb_data, pb_len);

            if (network_present) { 
                /* 
                * If SSL verification isn't needed for testing:
                * - CONFIG_ESP_TLS_INSECURE=y
                * - CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY=y
                * 
                * Otherwise add:
                * - CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
                * - #include "esp_crt_bundle.h"
                * - esp_http_client_config_t http_client_config = {
                *     .crt_bundle_attach = esp_crt_bundle_attach,
                *   }
                */
                esp_http_client_config_t http_client_config = {
                    .url = OTLP_METRICS_ENDPOINT,
                    .timeout_ms = 1000,
                    .crt_bundle_attach = esp_crt_bundle_attach,
                };

                printf("[HTTP]: Init\n");
                esp_http_client_handle_t client = esp_http_client_init(&http_client_config);
                esp_http_client_set_method(client, HTTP_METHOD_POST);
                esp_http_client_set_header(client, "Content-Type", "application/x-protobuf");
                esp_http_client_set_header(client, "X-SF-Token", X_SF_TOKEN);
                esp_http_client_set_post_field(client, pb_data, pb_len);
                printf("[HTTP]: Perform\n");
                err = esp_http_client_perform(client);
                if (err == ESP_OK) {
                    printf("HTTP Status = %d\n", esp_http_client_get_status_code(client));
                } else {
                    printf( "HTTP Error = %s\n", esp_err_to_name(err));
                }
                printf("[HTTP]: Clean-up\n");
                esp_http_client_cleanup(client);
            }

            free(pb_data);
        }
        ulp_state = 0; // Set back to running
        sx127x_write_single(spi, 0x3E, 0x08); // Reset RSSI
        sx127x_rxrestart(spi);
    }
}

/**
 * @brief Interrupt for when the GPIO_RX_DONE goes low, indicating RX complete
 * @param arg SPI Device Handle
 */
static void IRAM_ATTR sx1278_433_task_isr(void *arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    vTaskNotifyGiveFromISR(sx1278_433_task_handle, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
} 

/**
 * @brief Initialise RTC and ULP
 * @param spi SPI Device Handle
 * @param reg Register to interrogate
 * @return `uint8_t` Data
 */
void init_rtc_and_ulp(void)
{
    esp_err_t err;

    /*
     * RFM DATA GPIO
     */
    gpio_num_t gpio_num = PIN_NUM_433_DIO2;
    assert(rtc_gpio_is_valid_gpio(gpio_num) && "Not a valid RTC GPIO");

    // Initialise GPIO as RTC GPIO
    err = rtc_gpio_init(gpio_num);
    ESP_ERROR_CHECK(err);

    // Set the RTC GPIO to INPUT
    rtc_gpio_set_direction(gpio_num, RTC_GPIO_MODE_INPUT_ONLY);
    ESP_ERROR_CHECK(err);

    // Change GPIO, removing pullups and pulldowns
    rtc_gpio_pulldown_dis(gpio_num);
    rtc_gpio_pullup_dis(gpio_num);

    /*
     * ULP Receive Signalling GPIO
     */
    gpio_num = GPIO_RX_DONE;
    assert(rtc_gpio_is_valid_gpio(gpio_num) && "Not a valid RTC GPIO");

    // Initialise GPIO as RTC GPIO
    err = rtc_gpio_init(gpio_num);
    ESP_ERROR_CHECK(err);

    // Set the RTC GPIO to INPUT & OUTPUT (main program & ULP respectively)
    rtc_gpio_set_direction(gpio_num, RTC_GPIO_MODE_INPUT_OUTPUT);
    ESP_ERROR_CHECK(err);

    /*
     * ULP Program Upload
     */
    err = ulp_load_binary(0, bin_start, (bin_end - bin_start) / sizeof(uint32_t));
    ESP_ERROR_CHECK(err);
}

void init_sx1278_433_ook_raw(void) { 
    esp_err_t err;

    static spi_device_handle_t sx1278_433_spi;
    spi_device_interface_config_t sx1278_433_cfg = {
        .command_bits = 8,
        .clock_speed_hz = SPI_MASTER_FREQ_10M,
        .mode = 0,
        .spics_io_num = PIN_NUM_433_CS,
        .queue_size = 1,
    };
    err = spi_bus_add_device(SPI_HOST_ID, &sx1278_433_cfg, &sx1278_433_spi);

    gpio_set_direction(PIN_NUM_433_RST, GPIO_MODE_OUTPUT);

    // SX1278 433MHz RSSI GPIO - ISR for signal detection
    gpio_set_direction(PIN_NUM_433_DIO0, GPIO_MODE_INPUT);
    gpio_set_intr_type(PIN_NUM_433_DIO0, GPIO_INTR_POSEDGE);
    gpio_pulldown_dis(PIN_NUM_433_DIO0);
    gpio_pullup_dis(PIN_NUM_433_DIO0);

    sx127x_init_ookcontinuous(sx1278_433_spi, PIN_NUM_433_RST, 433915000, 2000);
    
    // SX1278 433MHz DATA GPIO - Read by ULP, but calibrate first
    gpio_set_direction(PIN_NUM_433_DIO2, GPIO_MODE_INPUT);
    sx127x_write_single(sx1278_433_spi, 0x15, 1);
    sx127x_rssithresh_calibrate(sx1278_433_spi);
    // Reset RSSI before we attach interrupt
    sx127x_write_single(sx1278_433_spi, 0x3E, 0x08); // Reset RSSI

    // SX1278 433MHz RSSI GPIO - ISR for ULP RX Complete
    gpio_set_direction(GPIO_RX_DONE, GPIO_MODE_INPUT);
    gpio_set_intr_type(GPIO_RX_DONE, GPIO_INTR_NEGEDGE);
    gpio_pulldown_en(GPIO_RX_DONE);

    // Ready the ULP, but do not run yet
    init_rtc_and_ulp();

    // Start 433 post-processor task
    xTaskCreate(sx1278_433_task, "433MHz Task", 4096, (void *)sx1278_433_spi, 10, &sx1278_433_task_handle);

    // Attach the interrupt service routine, dio0_rssi_isr(), to DIO0
    err = gpio_isr_handler_add(PIN_NUM_433_DIO0, sx1278_433_ulp_isr, NULL);
    ESP_ERROR_CHECK(err);

    // Attach the interrupt service routine, rx_done_isr(), to GPIO_RX_DONE
    err = gpio_isr_handler_add(GPIO_RX_DONE, sx1278_433_task_isr, (void*)sx1278_433_spi);
    ESP_ERROR_CHECK(err);
}