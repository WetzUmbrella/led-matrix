#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "led_strip.h"
#include "driver/i2c_master.h"

#define LED_GPIO    14
#define LED_COUNT   64
#define I2C_SDA     11
#define I2C_SCL     12
#define BRIGHT      10

static const char *TAG = "boardtest";

static void fill(led_strip_handle_t s, uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < LED_COUNT; i++) led_strip_set_pixel(s, i, r, g, b);
    led_strip_refresh(s);
}

void app_main(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    bool imu_found = false;
    for (uint8_t addr = 0x6A; addr <= 0x6B; addr++) {
        if (i2c_master_probe(bus, addr, 100) == ESP_OK) {
            ESP_LOGI(TAG, "IMU: QMI8658 found at 0x%02X -> PASS", addr);
            imu_found = true;
        }
    }
    if (!imu_found) ESP_LOGE(TAG, "IMU: not found on I2C -> FAIL");

    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_COUNT,
        .led_model = LED_MODEL_WS2812,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    led_strip_handle_t strip;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &strip));

    while (1) {
        ESP_LOGI(TAG, "LEDs: red / green / blue / white, then pixel walk");
        fill(strip, BRIGHT, 0, 0);           vTaskDelay(pdMS_TO_TICKS(700));
        fill(strip, 0, BRIGHT, 0);           vTaskDelay(pdMS_TO_TICKS(700));
        fill(strip, 0, 0, BRIGHT);           vTaskDelay(pdMS_TO_TICKS(700));
        fill(strip, BRIGHT, BRIGHT, BRIGHT); vTaskDelay(pdMS_TO_TICKS(700));
        for (int i = 0; i < LED_COUNT; i++) {
            led_strip_clear(strip);
            led_strip_set_pixel(strip, i, 0, BRIGHT, BRIGHT);
            led_strip_refresh(strip);
            vTaskDelay(pdMS_TO_TICKS(40));
        }
    }
}
