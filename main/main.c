#include <stdio.h>

// ESP errors, logging, events
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"

// Timestamping
#include "esp_timer.h"

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

#include "sx127x.h"

#include "sx1278_433_ook_raw/sx1278_433_ook_raw.h"

#include "lvgl_ui/lvgl_ui.h"
#include "otlp_climate/otlp_climate.h"

#include "config.h"

volatile bool network_present = false;

// Flag to indicate ULP has finished processing
volatile bool ulp_rx_done_433_flag = 0;

volatile bool gpio_rssi_868_flag = 0;

static spi_device_handle_t sx1278_433_spi;

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

    network_present = true;
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

void app_main(void)
{
    esp_err_t err;

    // Pause to avoid development-induced crash-loop
    // vTaskDelay(10000 / portTICK_PERIOD_MS);

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

    /* 
     * USB_OTG_EN | BUS_OUT_EN | Power Direction
     * -----------|------------|-----------------------------------------------------
     * 0x00       | 0x00       | USB 5V input,  BUS 5V input  (separate power)
     * 0x00       | 0x02       | USB 5V input,  BUS 5V output (to power a PoE base)§
     * 0x20       | 0x00       | USB 5V output, BUS 5V input  (to power a USB device)
     * 0x20       | 0x02       | USB 5V output, BUS 5V output (invalid?) 
     * 
     * §: To draw power from USB for a unpowered PoE, BOOST_EN needs to be enabled
     */

    /// CoreS3 - AW9523B GPIO: Port 0 = USB_OTG_EN (0x20) + BUS_OUT_EN (0x02)
    uint8_t bus_power_on[2] = { 0x02, 0x00 };
    err = i2c_master_transmit(aw9523b_handle, &bus_power_on[0], 2, -1);
    ESP_ERROR_CHECK(err);

    /// CoreS3 - AW9523B GPIO: Port 1 = BOOST_EN (0x80) | LCD_RST (0x02)
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

    // LCD setup
    lv_display_t *lvgl_disp = NULL;
    lvgl_display_init(lvgl_disp, SPI_HOST_ID, PIN_NUM_LCD_DC, PIN_NUM_LCD_CS);
    lvgl_port_lock(0);
    ui_main();
    lvgl_port_unlock();

    // W5500 setup - return a handle for debugging?
    esp_eth_handle_t *w5500poe_handle = setup_w5500poe(SPI_HOST_ID);
    assert(w5500poe_handle);

    /*
     * SX1278
     */
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

    init_rtc_and_ulp();
    init_433_isr_and_task(sx1278_433_spi);

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

    // SNTP
    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp_config);

    // Loop forever
    while(1) {
        vTaskDelay(5000 / portTICK_PERIOD_MS);

        if (gpio_rssi_868_flag) {
            gpio_rssi_868_flag = 0;
            printf("[SX127x] 868MHz RSSI Detected\n");
            uint8_t value = sx127x_read_single(sx1276_868_spi, 0x00);
            printf("[SX127x] 868MHz Packet Size: %d bytes\n", value);
            printf("[SX127x] 868MHz Packet Data:");
            uint8_t y = 0;
            for (int x = 0; x < value; x++) {
                y = sx127x_read_single(sx1276_868_spi, 0x00);
                printf(" %02x", y);
            }
            printf("\n");
        }
    }
}