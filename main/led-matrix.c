#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "esp_log.h"

static const char *TAG = "led_matrix";

#define LED_STRIP_GPIO_PIN      14
#define LED_STRIP_LED_COUNT     64                 // 8x8 WS2812 matrix
#define LED_STRIP_RMT_RES_HZ    (10 * 1000 * 1000) // 10MHz RMT tick resolution (standard for WS2812 timing)
#define STEP_DELAY_MS           100

// Brightness cap: the lit pixel's R=G=B channels are set directly to this
// value instead of scaling a full-brightness color at runtime.
// 25 / 255 ~= 9.8%, i.e. a dim white at ~10% max brightness.
#define MAX_BRIGHTNESS          25

void app_main(void)
{
    led_strip_handle_t led_strip;

    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO_PIN,
        .max_leds = LED_STRIP_LED_COUNT,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_STRIP_RMT_RES_HZ,
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_ERROR_CHECK(led_strip_clear(led_strip));

    ESP_LOGI(TAG, "LED matrix initialized on GPIO%d, %d LEDs, walking dot test starting",
             LED_STRIP_GPIO_PIN, LED_STRIP_LED_COUNT);

    // Walks a single lit pixel through all 64 indices in simple linear/raster
    // order (index 0 -> 63, following chain order as wired). This does NOT
    // account for serpentine/boustrophedon wiring some 8x8 matrices use where
    // alternate rows are reversed -- if the physical walk visibly "jumps back"
    // at a row boundary instead of continuing smoothly, the matrix is wired
    // serpentine and the index math would need a row/col remap.
    int current = 0;
    while (1) {
        ESP_ERROR_CHECK(led_strip_clear(led_strip));
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, current,
                                             MAX_BRIGHTNESS, MAX_BRIGHTNESS, MAX_BRIGHTNESS));
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));

        current = (current + 1) % LED_STRIP_LED_COUNT;
        vTaskDelay(pdMS_TO_TICKS(STEP_DELAY_MS));
    }
}
