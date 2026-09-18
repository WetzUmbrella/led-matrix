#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "led_matrix.h"
#include "esp_now_sync.h"

static const char *TAG = "led_matrix_wall";

#define GRID_W          16
#define GRID_H          16
#define GRID_LEDS       (GRID_W * GRID_H)
#define FRAME_PERIOD_US (1000000 / 60) // 60fps target
#define MAX_BRIGHTNESS  25             // ~10% of 255, matches the single-node demo

#if CONFIG_LM_POSITION_TOP_LEFT
#define OWN_POSITION LM_POS_TOP_LEFT
#elif CONFIG_LM_POSITION_TOP_RIGHT
#define OWN_POSITION LM_POS_TOP_RIGHT
#elif CONFIG_LM_POSITION_BOTTOM_LEFT
#define OWN_POSITION LM_POS_BOTTOM_LEFT
#else
#define OWN_POSITION LM_POS_BOTTOM_RIGHT
#endif

static led_strip_handle_t s_strip;

#if CONFIG_LM_ROLE_CONTROLLER

static TaskHandle_t s_render_task;

static void tick_timer_cb(void *arg)
{
    // esp_timer callbacks run in the (non-ISR) esp_timer task by default,
    // so a plain task notification is enough to wake the render task.
    xTaskNotifyGive(s_render_task);
}

// Maps a lit pixel in the shared 16x16 canvas to its quadrant + local index,
// then walks it one step further for the next tick. A single dot walking
// across all 256 positions (raster order) is deliberately simple: any
// stutter or misalignment at a quadrant seam is immediately visible, which
// is the point -- this is meant to expose sync problems, not hide them.
static void render_task(void *arg)
{
    uint8_t quadrant_rgb[LM_POS_COUNT][LM_QUADRANT_BYTES];
    uint32_t frame_seq = 0;
    int dot = 0;

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        memset(quadrant_rgb, 0, sizeof(quadrant_rgb));

        int x = dot % GRID_W;
        int y = dot / GRID_W;
        lm_position_t pos = (x < 8)
            ? (y < 8 ? LM_POS_TOP_LEFT : LM_POS_BOTTOM_LEFT)
            : (y < 8 ? LM_POS_TOP_RIGHT : LM_POS_BOTTOM_RIGHT);
        int local_idx = (y % 8) * 8 + (x % 8);
        quadrant_rgb[pos][local_idx * 3 + 0] = MAX_BRIGHTNESS;
        quadrant_rgb[pos][local_idx * 3 + 1] = MAX_BRIGHTNESS;
        quadrant_rgb[pos][local_idx * 3 + 2] = MAX_BRIGHTNESS;

        for (int p = 0; p < LM_POS_COUNT; p++) {
            if (p == OWN_POSITION) {
                continue; // rendered locally below, never sent to self
            }
            lm_espnow_send_quadrant((lm_position_t)p, frame_seq, quadrant_rgb[p]);
        }
        lm_led_matrix_show(s_strip, quadrant_rgb[OWN_POSITION]);

        frame_seq++;
        dot = (dot + 1) % GRID_LEDS;
    }
}

#else // peer

static void on_frame_received(const lm_frame_packet_t *packet)
{
    lm_led_matrix_show(s_strip, packet->pixels);
}

#endif

void app_main(void)
{
    lm_espnow_init();
    s_strip = lm_led_matrix_init(14);

#if CONFIG_LM_ROLE_CONTROLLER
    xTaskCreate(render_task, "render", 4096, NULL, configMAX_PRIORITIES - 2, &s_render_task);
    lm_espnow_register_peers();

    const esp_timer_create_args_t timer_args = {
        .callback = &tick_timer_cb,
        .name = "frame_tick",
    };
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, FRAME_PERIOD_US));
    ESP_LOGI(TAG, "Controller running: position %d, target %d fps",
             OWN_POSITION, 1000000 / FRAME_PERIOD_US);
#else
    lm_espnow_set_receive_handler(OWN_POSITION, on_frame_received);
    ESP_LOGI(TAG, "Peer running: waiting for quadrant %d frames over ESP-NOW", OWN_POSITION);
#endif
}
