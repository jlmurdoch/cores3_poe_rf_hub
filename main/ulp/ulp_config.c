#include "ulp_config.h"

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