#include <stdio.h>

// ESP errors, logging, events
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"

// GPIO
#include "driver/gpio.h"
#include "soc/gpio_struct.h"

// I2C
#include "driver/i2c_master.h"

// LCD drivers
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "esp_random.h" 

// Ethernet drivers
#include "esp_eth.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_eth_phy_w5500.h"
#include "esp_eth_mac_w5500.h"

// SNTP
#include "esp_netif_sntp.h"

// HTTP 
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

// ULP for raw radio
#include "ulp.h"
#include "ulp/ulp_config.h"
#include "ulp_data_433.h" 

// RTC GPIO
#include "driver/rtc_io.h"
#include "soc/rtc_cntl_reg.h"

#include "sx127x.h"

#include "lvgl_ui/lvgl_ui.h"
#include "otlp_climate/otlp_climate.h"

#include "config.h"

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
#define PIN_NUM_433_CS  18
#define PIN_NUM_433_RST  17
#define PIN_NUM_433_DIO0  8
#define PIN_NUM_433_DIO2  2

// RA-01
#define PIN_NUM_868_CS  6
#define PIN_NUM_868_RST  5
#define PIN_NUM_868_DIO0  10

// Ethernet pins
#define PIN_NUM_ETH_CS      9
#define PIN_NUM_ETH_RST     7
#define PIN_NUM_ETH_INT     14

// Flag to indicate ULP has finished processing
volatile bool gpio_rssi_433_flag = 0;
volatile bool ulp_rx_done_433_flag = 0;

volatile bool gpio_rssi_868_flag = 0;

// Start and end addr of ULP program in 32bit chunks: 0 (0x0000) - 255 (0x03FF)
extern const uint8_t bin_start[] asm("_binary_ulp_data_433_bin_start");
extern const uint8_t bin_end[]   asm("_binary_ulp_data_433_bin_end");

static esp_event_handler_instance_t eth_ev_instance = NULL;

// Event handler for Ethernet events
static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    // we can get the ethernet driver handle from event data
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                    mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

// Event handler for TCP/IP events
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
    int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~");
}

/**
 * @brief Interrupt for when RSSI goes high on DIO2
 * @param arg Unused
 */
static void IRAM_ATTR dio0_rssi_433_isr(void *arg) {
    esp_err_t err;
    gpio_rssi_433_flag = 1;
    err = ulp_run(&ulp_entry - RTC_SLOW_MEM);
    ESP_ERROR_CHECK(err);
}

/**
 * @brief Interrupt for when the GPIO_RX_DONE goes low, indicating RX complete
 * @param arg SPI Device Handle
 */
static void IRAM_ATTR rx_done_433_isr(void *arg) {
    spi_device_handle_t spi = (spi_device_handle_t)arg;
    if (ulp_state == 2) {
        ulp_rx_done_433_flag = 1;
    }
    ulp_state = 0;
    sx127x_rxrestart(spi);
}  

/**
 * @brief Interrupt for when RSSI goes high on DIO2
 * @param arg Unused
 */
static void IRAM_ATTR dio0_rssi_868_isr(void *arg) {
    gpio_rssi_868_flag = 1;
    // TODO: Push task
}

esp_eth_handle_t *setup_w5500poe(esp_lcd_spi_bus_handle_t spi_host_id) {
    esp_err_t err = ESP_OK;

    /*
     * Start Physical layer
     */ 
    // Config PHY and MAC
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    mac_config.rx_task_stack_size = 3072;
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = 1;
    phy_config.reset_gpio_num = PIN_NUM_ETH_RST;

    // Install interrupt service for W5500
    err = gpio_install_isr_service(0);
    ESP_ERROR_CHECK(err);

    // SPI configuration for the device
    spi_device_interface_config_t spidev_config = {
        .command_bits = 16,
        .address_bits = 8,
        .clock_speed_hz = 33 * 1000 * 1000, // Minimum reliable speed
        .mode = 0, 
        .spics_io_num = PIN_NUM_ETH_CS,
        .queue_size = 20,
    };

    // W5500 Ethernet SPI driver
    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(spi_host_id, &spidev_config); 
    w5500_config.int_gpio_num = PIN_NUM_ETH_INT;
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);

    // Install the event-driven Ethernet Driver
    esp_eth_handle_t *eth_handle = NULL;
    eth_handle = calloc(1, sizeof(esp_eth_handle_t));
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    err = esp_eth_driver_install(&eth_config, eth_handle);
    ESP_ERROR_CHECK(err);

    // MAC Address Set
    uint8_t eth_mac[6] = {0};
    ESP_ERROR_CHECK(esp_read_mac(eth_mac, ESP_MAC_ETH));
    ESP_ERROR_CHECK(esp_eth_ioctl(*eth_handle, ETH_CMD_S_MAC_ADDR, eth_mac));

    /* 
     * Start Network Layer
     */
    err = esp_netif_init();
    ESP_ERROR_CHECK(err);

    // Background event loop
    err = esp_event_loop_create_default();
    ESP_ERROR_CHECK(err);

    // Netif setup defaults
    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *netif = NULL;
    netif = esp_netif_new(&netif_config);
    assert(netif);

    // Attach Network to Data Link
    esp_eth_netif_glue_handle_t eth_netif_glue = esp_eth_new_netif_glue(*eth_handle);
    assert(eth_netif_glue);

    err = esp_netif_attach(netif, eth_netif_glue);
    ESP_ERROR_CHECK(err);

    /* 
     * EVENT HANDLING
     */
    
    // Add the Ethernet event handler
    err = esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL, &eth_ev_instance); 
    ESP_ERROR_CHECK(err);

    // Add the IP event handler
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL, &eth_ev_instance);
    ESP_ERROR_CHECK(err);

    // Now start OSI Layer 2: Data Link
    err = esp_eth_start(*eth_handle);
    ESP_ERROR_CHECK(err);

    return eth_handle;
}

/*
 ******** Generic SPI ********
 */

void setup_spi_bus(esp_lcd_spi_bus_handle_t spi_host_id) 
{
    esp_err_t err;

    // SPI bus configuration
    spi_bus_config_t bus_cfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_IMG_SIZE, // LCD canvas 
    };
    err = spi_bus_initialize(spi_host_id, &bus_cfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(err);
}

/*
 ******** I2C Setup ********
 */

 void setup_i2c_bus(void) {
    esp_err_t err;

    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = 0,
        .scl_io_num = PIN_NUM_I2C_SCL,
        .sda_io_num = PIN_NUM_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t i2c_bus_handle;
    err = i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle);
    ESP_ERROR_CHECK(err);    
}

i2c_master_dev_handle_t setup_i2c_dev(i2c_port_num_t i2c_port_num, uint16_t i2c_addr) 
{
    esp_err_t err;

    i2c_master_bus_handle_t i2c_bus_handle;
    err = i2c_master_get_bus_handle(0, &i2c_bus_handle);
    ESP_ERROR_CHECK(err);

    i2c_device_config_t i2c_dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_addr,
        .scl_speed_hz = I2C_FREQ,
    };

    i2c_master_dev_handle_t i2c_dev_handle;
    err = i2c_master_bus_add_device(i2c_bus_handle, &i2c_dev_config, &i2c_dev_handle);
    ESP_ERROR_CHECK(err);

    return i2c_dev_handle;
}

/**
 * @brief Initialise RTC and ULP
 * @param spi SPI Device Handle
 * @param reg Register to interrogate
 * @return `uint8_t` Data
 */
static void init_rtc_and_ulp(void)
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

void app_main(void)
{
    esp_err_t err;

    // Pause to avoid development-induced crash-loop
    vTaskDelay(10000 / portTICK_PERIOD_MS);

    // CoreS3 - Initialise I2C bus
    setup_i2c_bus();

    // CoreS3 - Prep the PMU and GPIO expander
    i2c_master_dev_handle_t axp2101_handle = setup_i2c_dev(I2C_PORT, I2C_ADDR_AXP2101);
    i2c_master_dev_handle_t aw9523b_handle = setup_i2c_dev(I2C_PORT, I2C_ADDR_AW9523B);

    // CoreS3 - Push/Pull on
    uint8_t aw_push_pull_on[2] = { 0x11, 0x10 };
    err = i2c_master_transmit(aw9523b_handle, &aw_push_pull_on[0], 2, -1);
    ESP_ERROR_CHECK(err);

    // CoreS3 - LCD Backlight Voltage - AXP2101: dldo1 voltage (0x99) = 100% brightness (0x17)
    uint8_t lcd_lite_voltage[2] = { 0x99, 0x17 };
    err = i2c_master_transmit(axp2101_handle, &lcd_lite_voltage[0], 2, -1);
    ESP_ERROR_CHECK(err);

    // CoreS3 - Read AXP2101 reg 0x90: LDOS ON/OFF
    uint8_t ldos_keypair[2] = { 0x90, 0x00 };
    err = i2c_master_transmit_receive(axp2101_handle, &ldos_keypair[0], 1, &ldos_keypair[1], 1, -1);
    ESP_ERROR_CHECK(err);

    // CoreS3 - Write AXP2101 reg 0x90: LCD Backlight on (bit7: dldo1)
    ldos_keypair[1] |= 0x80;
    err = i2c_master_transmit(axp2101_handle, &ldos_keypair[0], 2, -1);
    ESP_ERROR_CHECK(err);

    // CoreS3 - LCD Power On - PMU Port 1, LCD RST = HIGH
    uint8_t lcd_power_on[2] = { 0x03, 0x02 };
    err = i2c_master_transmit(aw9523b_handle, &lcd_power_on[0], 2, -1);
    ESP_ERROR_CHECK(err);

    // Pull SPI CS HIGH for all attached SPI devices
    gpio_set_direction(PIN_NUM_ETH_CS, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_NUM_SXC_CS, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_NUM_LCD_CS, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_NUM_868_CS, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_NUM_433_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_NUM_ETH_CS, 1);
    gpio_set_level(PIN_NUM_SXC_CS, 1);
    gpio_set_level(PIN_NUM_LCD_CS, 1);
    gpio_set_level(PIN_NUM_868_CS, 1);
    gpio_set_level(PIN_NUM_433_CS, 1);

    // Initialise SPI bus
    setup_spi_bus(SPI_HOST_ID);

    // W5500 setup - return a handle for debugging?
    esp_eth_handle_t *w5500poe_handle = setup_w5500poe(SPI_HOST_ID);
    assert(w5500poe_handle);

    /*
     * SX1278
     */
    spi_device_handle_t sx1278_433_spi;
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

    sx127x_init_ookcontinuous(sx1278_433_spi, PIN_NUM_433_RST, 433910000, 2000);

    // SX1278 433MHz DATA GPIO - Read by ULP
    gpio_set_direction(PIN_NUM_433_DIO2, GPIO_MODE_INPUT);

    // SX1278 433MHz RSSI GPIO - ISR for ULP RX Complete
    gpio_set_direction(GPIO_RX_DONE, GPIO_MODE_INPUT);
    gpio_set_intr_type(GPIO_RX_DONE, GPIO_INTR_NEGEDGE);
    gpio_pulldown_en(GPIO_RX_DONE);

    init_rtc_and_ulp();

    // Attach the interrupt service routine, dio0_rssi_isr(), to DIO0
    err = gpio_isr_handler_add(PIN_NUM_433_DIO0, dio0_rssi_433_isr, NULL);
    ESP_ERROR_CHECK(err);

    // Attach the interrupt service routine, rx_done_isr(), to GPIO_RX_DONE
    err = gpio_isr_handler_add(GPIO_RX_DONE, rx_done_433_isr, (void*)sx1278_433_spi);
    ESP_ERROR_CHECK(err);

    /*
     * SX1276
     */
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

    // Attach the interrupt service routine, dio0_rssi_isr(), to DIO0
    err = gpio_isr_handler_add(PIN_NUM_868_DIO0, dio0_rssi_868_isr, NULL);
    ESP_ERROR_CHECK(err);

    // LCD setup
    lv_display_t *lvgl_disp = NULL;
    lvgl_display_init(lvgl_disp, SPI_HOST_ID, PIN_NUM_LCD_DC, PIN_NUM_LCD_CS);
    lvgl_port_lock(0);
    ui_main();
    lvgl_port_unlock();

    // SNTP
    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp_config);

    // Loop forever
    while(1) {
        vTaskDelay(5000 / portTICK_PERIOD_MS);

        if (gpio_rssi_868_flag) {
            printf("[SX127x] 868MHz RSSI Detected\n");
            gpio_rssi_868_flag = 0;
            uint8_t value = sx127x_read_single(sx1276_868_spi, 0x00);
            printf("[SX127x] 868MHz Packet Size: %d bytes\n", value);
            printf("[SX127x] 868MHz Packet Data:");
            for (int x = 0; x < value; x++) {
                printf(" %02x", sx127x_read_single(sx1276_868_spi, 0x00));
            }
            printf("\n");
        }
        
        if (gpio_rssi_433_flag) {
            printf("[SX127x] 433MHz RSSI Detected\n");
            gpio_rssi_433_flag = 0;
        }
        if (ulp_rx_done_433_flag) {
            ulp_rx_done_433_flag = 0;

            // Print the RFM ULP variables
            printf("[SX127x] 433MHz Raw Data: 0x%04x,0x%04x\n", 
                (uint16_t)(&ulp_value)[0], (uint16_t)(&ulp_value)[1]);

            int64_t sensor_id = ((uint16_t)(&ulp_value)[0] >> 8) & 0xFF;
            uint8_t battery = ((uint16_t)(&ulp_value)[0] >> 7) & 0x1; 
            uint8_t manual = ((uint16_t)(&ulp_value)[0] >> 6) & 0x1;
            uint8_t channel = (((uint16_t)(&ulp_value)[0]>> 4) & 0x3) + 1;
            double temp = ((((uint16_t)(&ulp_value)[0] & 0xF) << 16) |
                            (((uint16_t)(&ulp_value)[1] >> 8) & 0xFF)) * 0.1;
            int64_t humidity = ((uint16_t)(&ulp_value)[1] & 0xFF);
            printf("[SX127x] 433MHz Parsed Data: Id: 0x%02llx, Bat: %d, Man: %d, Ch: %d, Temp: %.1f°C, RH: %lld%%\n", 
                sensor_id, battery, manual, channel, temp, humidity);

            char *pb_data = (char *)malloc(sizeof(char) * 1024);
            size_t pb_len = otlp_climate(pb_data, sensor_id, temp, humidity);
            otlp_nanopb_print(pb_data, pb_len);

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
                .timeout_ms = 5000,
                .crt_bundle_attach = esp_crt_bundle_attach,
            };

            esp_http_client_handle_t client = esp_http_client_init(&http_client_config);
            esp_http_client_set_method(client, HTTP_METHOD_POST);
            esp_http_client_set_header(client, "Content-Type", "application/x-protobuf");
            esp_http_client_set_header(client, "X-SF-Token", X_SF_TOKEN);
            esp_http_client_set_post_field(client, pb_data, pb_len);
            err = esp_http_client_perform(client);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "HTTP Status = %d", esp_http_client_get_status_code(client));
            } else {
                ESP_LOGE(TAG, "HTTP Error = %s", esp_err_to_name(err));
            }
            esp_http_client_cleanup(client);

            free(pb_data);
        }
    }
}