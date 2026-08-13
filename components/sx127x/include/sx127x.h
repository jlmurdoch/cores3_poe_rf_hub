#ifndef SX127X_H
#define SX127X_H

#include <string.h>             // memset()
#include "driver/spi_master.h"  // SPI
#include "freertos/FreeRTOS.h"  
#include "freertos/task.h"      // portMAX_DELAY
#include "driver/gpio.h"        // for reset

#define FXOSC 32000000.0

enum { RXBWMANT_16, RXBWMANT_20, RXBWMANT_24 };
enum { RXBWEXP_0, RXBWEXP_1, RXBWEXP_2,  RXBWEXP_3,  RXBWEXP_4, RXBWEXP_5,  RXBWEXP_6,  RXBWEXP_7 };

uint8_t sx127x_read_single(spi_device_handle_t spi, uint8_t reg);
void sx127x_write_single(spi_device_handle_t spi, uint8_t reg, uint8_t value);
void sx127x_init_ookcontinuous(spi_device_handle_t spi, gpio_num_t gpio_rst, uint32_t frequency, uint16_t bitrate);
void sx127x_init_fskpacket(spi_device_handle_t spi, gpio_num_t gpio_rst, uint32_t frequency, uint16_t bitrate);
void sx127x_ookfixthresh_calibrate(spi_device_handle_t spi, gpio_num_t gpio_dio2, uint32_t bitrate);
void sx127x_rxrestart(spi_device_handle_t spi);
void sx127x_rssithresh_calibrate(spi_device_handle_t spi);

#endif