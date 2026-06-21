#include "freertos/FreeRTOS.h"  
#include "freertos/task.h"      // portMAX_DELAY

#include "esp_check.h"

#include "esp_lcd_ili9341.h"

#include "lvgl.h"
#include "esp_lvgl_port.h"

#define LVGL_HRES 320
#define LVGL_VRES 240

static const char *TAG = "CORES3_POE_RF_HUB";

esp_err_t lvgl_display_init(lv_display_t *disp_handle, esp_lcd_spi_bus_handle_t spi_bus, gpio_num_t dc_pin, gpio_num_t cs_pin);
void ui_main(void);