#include "sx127x.h"

/**
 * @brief SX127x base SPI command
 * @param spi SPI Device Handle
 * @param cmd Register to interrogate
 * @param write Flag to indicate a write procedure
 * @param buf Data buffer for reading into or writing from
 * @param len Data buffer length in bytes
 */
void sx127x_cmd(spi_device_handle_t spi, const uint8_t cmd, uint8_t write, uint8_t *buf, size_t len) 
{
    esp_err_t err;
    err = spi_device_acquire_bus(spi, portMAX_DELAY);
    assert(err == ESP_OK);

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));

    t.cmd = (write << 7) | cmd;
    
    t.length = 8 * len;
    if (write) {
        t.tx_buffer = buf;
    } else {
        t.rx_buffer = buf;
        t.rxlength = 8 * len;
    }
    err = spi_device_polling_transmit(spi, &t);
    assert(err == ESP_OK);

    spi_device_release_bus(spi);
}

/**
 * @brief RFM Read with single byte returned
 * @param spi SPI Device Handle
 * @param reg Register to interrogate
 * @return `uint8_t` Data
 */
uint8_t sx127x_read_single(spi_device_handle_t spi, uint8_t reg) {
    uint8_t buf;
    sx127x_cmd(spi, reg, 0, &buf, 1);

    return buf;
}

/**
 * @brief SX127x Write with single byte
 * @param spi SPI Device Handle
 * @param reg Register to interrogate
 * @param value Data to write
 */
void sx127x_write_single(spi_device_handle_t spi, uint8_t reg, uint8_t value) {
    sx127x_cmd(spi, reg, 1, &value, 1);
}

/**
 * @brief SX127x initialisation procedure for OOK Continuous
 * @param spi SPI Device Handle
 * @param gpio_rst GPIO to reset the RFM
 * @param frequency Frequency in Hz to tune into
 * @param bitrate Bitrate in bits-per-second
 */
void sx127x_init_ookcontinuous(spi_device_handle_t spi, gpio_num_t gpio_rst, uint32_t frequency, uint16_t bitrate) {
    // bandwidth, FSK/OOK, bitsync, continuous/packet

    // Perform Reset
    printf("[SX127x] Reset: Start\n");
    gpio_set_level(gpio_rst, 0);
    esp_rom_delay_us(100);
    gpio_set_level(gpio_rst, 1);
    esp_rom_delay_us(5000);
    printf("[SX127x] Reset: Done\n");

    // Check version
    if (sx127x_read_single(spi, 0x42) != 0x12){
        printf("[SX127x] Version: FAILED\n");
        return;
    }
    printf("[SX127x] Version: OK\n");

    // OpMode: Sleep
    sx127x_write_single(spi, 0x01, 0x28); // LoRa off, OOK, Low Freq mode, Sleep
    while((sx127x_read_single(spi, 0x3E) >> 7) & 0x1); // Wait for Mode Ready Cleared
    printf("[SX127x] OpMode: Sleep\n");

    // Bitrate
    uint16_t raw_bitrate = FXOSC / bitrate;
    sx127x_write_single(spi, 0x02, (raw_bitrate >> 8) & 0xFF);
    sx127x_write_single(spi, 0x03, raw_bitrate & 0xFF);

    // Frequency
    uint32_t raw_freq = (uint32_t)(frequency / (FXOSC / 524288));
    sx127x_write_single(spi, 0x06, (raw_freq >> 16) & 0xFF);
    sx127x_write_single(spi, 0x07, (raw_freq >> 8) & 0xFF);
    sx127x_write_single(spi, 0x08, raw_freq & 0xFF);

    // Bandwidth
    sx127x_write_single(spi, 0x12, RXBWMANT_24 << 3 | RXBWEXP_4);
    // AFC Bandwidth
    sx127x_write_single(spi, 0x13, RXBWMANT_24 << 3 | RXBWEXP_4);

    sx127x_write_single(spi, 0x14, 0x28); // RegOokPeak: Bit sync on, OOK peak, OOKPeakThresStep 0.5dB
    sx127x_write_single(spi, 0x31, 0x00); // RegPacketConfig2: Packet mode off, Continuous on
    sx127x_write_single(spi, 0x40, 0x40); // RegDioMapping1: DIO0 = RSSI
    sx127x_write_single(spi, 0x0C, 0x23); // RegLNA:  Max gain + boost of 150%
    sx127x_write_single(spi, 0x0D, 0x00); // RegRxConfig: RX restart off
    sx127x_write_single(spi, 0x1F, 0x00); // RegPreambleDetect: Preamble off

    // Bring SX127x out of sleep into frequency synthesis receive mode
    sx127x_write_single(spi, 0x01, 0x2C); // Set freq synth receiver mode
    while(!((sx127x_read_single(spi, 0x3E)) >> 7)); // Wait for Mode Ready Set
    printf("[SX127x] OpMode: Freq Synth Rx\n");

    // Bring SX127x out of frequency synthesis receive into receiving mode
    sx127x_write_single(spi, 0x01, 0x2D); // Set receiver mode
    while(!((sx127x_read_single(spi, 0x3E)) >> 7)); // Wait for Mode Ready Set  
    printf("[SX127x] OpMode: Rx\n");
}

/**
 * @brief SX127x initialisation procedure for OOK Continuous
 * @param spi SPI Device Handle
 * @param gpio_rst GPIO to reset the RFM
 * @param frequency Frequency in Hz to tune into
 * @param bitrate Bitrate in bits-per-second
 */
void sx127x_init_fskpacket(spi_device_handle_t spi, gpio_num_t gpio_rst, uint32_t frequency, uint16_t bitrate) {
    // bandwidth, FSK/OOK, bitsync, continuous/packet

    // Perform Reset
    printf("[SX127x] Reset: Start\n");
    gpio_set_level(gpio_rst, 0);
    esp_rom_delay_us(100);
    gpio_set_level(gpio_rst, 1);
    esp_rom_delay_us(5000);
    printf("[SX127x] Reset: Done\n");

    // Check version
    if (sx127x_read_single(spi, 0x42) != 0x12){
        printf("[SX127x] Version: FAILED\n");
        return;
    }
    printf("[SX127x] Version: OK\n");

    // OpMode: Sleep
    sx127x_write_single(spi, 0x01, 0x08); // LoRa off, OOK, Low Freq mode, Sleep
    while((sx127x_read_single(spi, 0x3E) >> 7) & 0x1); // Wait for Mode Ready Cleared
    printf("[SX127x] OpMode: Sleep\n");

    // Bitrate
    uint16_t raw_bitrate = FXOSC / bitrate;
    sx127x_write_single(spi, 0x02, (raw_bitrate >> 8) & 0xFF);
    sx127x_write_single(spi, 0x03, raw_bitrate & 0xFF);

    // Frequency
    uint32_t raw_freq = (uint32_t)(frequency / (FXOSC / 524288));
    sx127x_write_single(spi, 0x06, (raw_freq >> 16) & 0xFF);
    sx127x_write_single(spi, 0x07, (raw_freq >> 8) & 0xFF);
    sx127x_write_single(spi, 0x08, raw_freq & 0xFF);

    // Bandwidth
    sx127x_write_single(spi, 0x12, RXBWMANT_20 << 3 | RXBWEXP_5);
    // AFC bandwidth
    sx127x_write_single(spi, 0x13, RXBWMANT_20 << 3 | RXBWEXP_5);

    // RSSI signaling
    sx127x_write_single(spi, 0x40, 0x00); // Pin Mapping: DIO0 = RSSI

    // Data Mode & Modulation
    sx127x_write_single(spi, 0x31, 0x40); // Packet on, io-homecontrol off, beacon off, pkt-length msb = 0
    sx127x_write_single(spi, 0x32, 0x20); 

    sx127x_write_single(spi, 0x33, 0x8B); 
    sx127x_write_single(spi, 0x34, 0x0B); 

    sx127x_write_single(spi, 0x30, 0x84); // Var length, Payload Ready + Hold FIFO

    sx127x_write_single(spi, 0x27, 0x71); // Auto restart after payload ready, 0x55 preamble, syncword 2bytes, 0x2DD4
    sx127x_write_single(spi, 0x28, 0x2D);
    sx127x_write_single(spi, 0x29, 0xD4); 
    
    // Bring SX127x out of sleep into frequency synthesis receive mode
    sx127x_write_single(spi, 0x01, 0x0C); // Set freq synth receiver mode
    while(!((sx127x_read_single(spi, 0x3E)) >> 7)); // Wait for Mode Ready Set
    printf("[SX127x] OpMode: Freq Synth Rx\n");

    // Bring SX127x out of frequency synthesis receive into receiving mode
    sx127x_write_single(spi, 0x01, 0x0D); // Set receiver mode
    while(!((sx127x_read_single(spi, 0x3E)) >> 7)); // Wait for Mode Ready Set  
    printf("[SX127x] OpMode: Rx\n");
}

/**
 * @brief SX OOK Floor Threshold Optimize
 * @param spi SPI Device Handle
 */
void sx127x_ookfixthresh_calibrate(spi_device_handle_t spi, gpio_num_t gpio_dio2, uint32_t bitrate) {
    uint8_t glitch = 3;
    uint8_t threshold = 0;
    int level = 0;

    while (glitch > 1) {
        glitch = 0;

        // Reset / Increment OOK threshold
        threshold++;
        printf("Threshold adjust: +%ddBi\n", threshold);
        sx127x_write_single(spi, 0x15, threshold);

        // Try three times to get a glitch-free reading
        for (int x = 0; x < 3; x++) {
            level = gpio_get_level(gpio_dio2);

            // Sample 1000 times
            for (int y = 0; y < 30; y++) {
                // Sample changes in bits according to bitrate
                vTaskDelay(10 / portTICK_PERIOD_MS);
                // If high, flag as glitch
                if (gpio_get_level(gpio_dio2) != level) {
                    glitch++;
                    break;
                }
            }
        }
    }
    printf("Threshold Final: +%ddBi\n", threshold);
}

/**
 * @brief Compare values
 * @param a First value
 * @param b Second value
 */
int compare_uint8(const void *a, const void *b) {
    return (*(int *)a - *(int *)b);
}

/**
 * @brief SX RSSI Threshold Calibrate
 * @param spi SPI Device Handle
 */
void sx127x_rssithresh_calibrate(spi_device_handle_t spi) {
    uint8_t rssiValues[21];

    for (int x = 0; x < (sizeof(rssiValues) / sizeof(rssiValues[0])); x++) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        rssiValues[x] = sx127x_read_single(spi, 0x11);
    }
    qsort(rssiValues, sizeof(rssiValues) / sizeof(rssiValues[0]), sizeof(rssiValues[0]), compare_uint8);

    // Set Median as threshold
    printf("[SX127x] RSSI Threshold: %0.1f dB (0x%02x)\n", (float)(rssiValues[10] - 12) / 2, rssiValues[10] - 12);
    sx127x_write_single(spi, 0x10, rssiValues[10] - 12);
}

/**
 * @brief RFM RxRestart
 * @param spi SPI Device Handle
 */
void sx127x_rxrestart(spi_device_handle_t spi) {
    uint8_t value = 0x00;
    sx127x_cmd(spi, 0x0D, 0, &value, 1);
    // Restart without PLL / changes
    value |= 0x40;
    sx127x_cmd(spi, 0x0D, 1, &value, 1);
    
}