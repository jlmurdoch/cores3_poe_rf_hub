#ifndef SX1278_433_OOK_RAW_H
#define SX1278_433_OOK_RAW_H

#include "esp_attr.h"
#include "esp_err.h"
#include "ulp/ulp_config.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"  
#include "freertos/task.h"
#include "esp_timer.h"
#include "otlp_climate/otlp_climate.h"
// HTTP 
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "sx127x.h"

#include "esp_err.h"
#include "driver/gpio.h"
#include "soc/gpio_struct.h"

// RTC GPIO
#include "driver/rtc_io.h"
#include "soc/rtc_cntl_reg.h"

// ULP for raw radio
#include "ulp.h"
#include "ulp_data_433.h" 
#include "config.h"

extern volatile bool network_present;
static TaskHandle_t sx1278_433_task_handle = NULL;
// Start and end addr of ULP program in 32bit chunks: 0 (0x0000) - 255 (0x03FF)
extern const uint8_t bin_start[] asm("_binary_ulp_data_433_bin_start");
extern const uint8_t bin_end[]   asm("_binary_ulp_data_433_bin_end");

void init_rtc_and_ulp(void);

void init_433_isr_and_task(spi_device_handle_t sx1278_433_spi);

#endif