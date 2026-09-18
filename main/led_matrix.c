#include "led_matrix.h"
#include "esp_err.h"

led_strip_handle_t lm_led_matrix_init(int gpio_num)
{
    led_strip_handle_t strip;

    led_strip_config_t strip_config = {
        .strip_gpio_num = gpio_num,
        .max_leds = LM_MATRIX_LEDS,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &strip));
    ESP_ERROR_CHECK(led_strip_clear(strip));
    return strip;
}

void lm_led_matrix_show(led_strip_handle_t strip, const uint8_t *pixels)
{
    for (int i = 0; i < LM_MATRIX_LEDS; i++) {
        ESP_ERROR_CHECK(led_strip_set_pixel(strip, i,
                                             pixels[i * 3 + 0],
                                             pixels[i * 3 + 1],
                                             pixels[i * 3 + 2]));
    }
    ESP_ERROR_CHECK(led_strip_refresh(strip));
}
