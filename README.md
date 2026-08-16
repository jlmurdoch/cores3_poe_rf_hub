# M5Stack CoreS3 with PoE and RF 

This work-in-progress M5Stack Core setup that is:
- Designed to be mounted on a DIN Rail
- Powered by Power-over-Ethernet (PoE)
- Collects a whole manner of RF transmissions:
  - 2.4GHz Wireless 
  - Bluetooth (Passive scanning for GAP Advertisements)
  - 433MHz OOK raw signals
  - 868MHz FSK packets (not LoRa)

This is a demonstration of the use of interrupts and/or the ULP to offload signal processing with two radios, one handling packets, another doing raw signal processing (AKA "continuous").

Development was blocked by a [SPI Contention issue between the Ethernet and LCD](https://github.com/espressif/esp-idf/issues/15510), but then that uncovered a subsequent [SPI assertion issue](https://github.com/espressif/esp-idf/issues/17860). Both issues should be resolved in all ESP-IDF versions > 6.0.1. 

## M5Stack Hardware

Layer | Shim          | Primary Chipset  | Function
------|---------------|----------|---------
1 :white_square_button: | [CoreS3 SE](https://docs.m5stack.com/en/core/M5CoreS3%20SE)     | [Espressif ESP32-S3](https://www.espressif.com/en/products/socs/esp32-s3/) | Central compute + WiFi/BLE reception + Display
2 :blue_square: | [Module LoRa433 v1.0](https://docs.m5stack.com/en/module/lora)§ | [Ai-Thinker RA-02](https://docs.ai-thinker.com/en/Ra-02/index.html) / [Semtech SX1278](https://www.semtech.com/products/wireless-rf/lora-connect/sx1278) | Weather station reception
3 :heavy_minus_sign: | [Module LoRa868 v1.1](https://docs.m5stack.com/en/module/Module-LoRa868_V1.1) | [Ai-Thinker RA-01H](https://docs.ai-thinker.com/en/Ra-01H/) / [Semtech SX1276](https://www.semtech.com/products/wireless-rf/lora-connect/sx1276) | Heating system reception
4 :white_medium_small_square: | [Base LAN PoE v1.2](https://docs.m5stack.com/en/base/lan_poe_v12) | [WIZnet W5500](https://wiznet.io/products/ethernet-chips/w5500) | Power + Ethernet data transmission

> [!IMPORTANT]
> § The LoRa433 module has been custom-modified, rerouting pins and notably adding support for sampling raw data from DIO2.

During desk-testing, the stack was powered with a 12V supply.

## Implementation Details

Short detail at present:
- :white_square_button: **ILI9341 Display** - handled by the CoreS3-SE using the LVGL library for graphics.
- :wireless: **Bluetooth Low Energy (BLE)** - handled by the CoreS3-SE, using the ESP-IDP port of the NimBLE library to scan for GAP advertisements.
- :wireless: **SX1278 433MHz** - The SX1278 is tuned to receive raw OOK and it is outputted onto the DIO2 pin, where the [ESP32-S3 ULP](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/ulp-fsm.html) handles the signal processing / timing using assembler.
- :wireless: **SX1276 868MHz** - The SX1276 is tuned to receive packets, using [interrupts](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/intr_alloc.html) to signal to the main program when valid packets are received.
- :zap: **W5500 PoE** - Handled by the WIZnet W5500 and main program. 

# Changes / learnings
- The LCD display updates can be the cause of watchdog timeouts
- The function `esp_http_client_perform()` has to be implemented outside the main task (`app_main()`):
  - Stack limits (`ESP_MAIN_TASK_STACK_SIZE = 3584`) cause a stack overflow. 
  - TLS hardware acceleration (`MBEDTLS_HARDWARE_MPI`) creates `wdt timeout` issues.
