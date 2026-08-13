#ifndef CONFIG_H
#define CONFIG_H

#define OTLP_METRICS_ENDPOINT "https://ingest.eu2.observability.splunkcloud.com/v2/datapoint/otlp"
#define X_SF_TOKEN ""

// I2C Basics
#define PIN_NUM_I2C_SDA 12
#define PIN_NUM_I2C_SCL 11
#define I2C_FREQ 400000
#define I2C_PORT 0

// I2C Addresses
#define I2C_ADDR_AXP2101 0x34 // PMU
#define I2C_ADDR_AW9523B 0x58 // GPIO Expander

// Common SPI Defines
#define SPI_HOST_ID     SPI2_HOST
#define PIN_NUM_MISO    35
#define PIN_NUM_MOSI    37
#define PIN_NUM_CLK     36 

// SD Card Reader
#define PIN_NUM_SXC_CS      4

// LCD pins
#define PIN_NUM_LCD_CS      3
#define PIN_NUM_LCD_DC      35 

// LCD Attributes
#define LCD_H_RES 320
#define LCD_V_RES 240
#define LCD_H_OFF 0
#define LCD_V_OFF 0
#define LCD_IMG_SIZE (LCD_H_RES * LCD_V_RES * sizeof(uint16_t))

// LCD backlight via AXP2101 PMU (I2C)
// LCD reset via AW9523B GPIO Expander (I2C)

/*
 * M5Stack Shims
 */

// RA-02
#define PIN_NUM_433_CS   18
#define PIN_NUM_433_RST  17
#define PIN_NUM_433_DIO0  8
#define PIN_NUM_433_DIO2  2

// RA-01
#define PIN_NUM_868_CS    6
#define PIN_NUM_868_RST   5
#define PIN_NUM_868_DIO0 10

// Ethernet pins
#define PIN_NUM_ETH_CS    9
#define PIN_NUM_ETH_RST   7
#define PIN_NUM_ETH_INT  14

#endif