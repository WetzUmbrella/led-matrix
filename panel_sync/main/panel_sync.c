// LED wall panel firmware (same firmware on every panel)
//  - joins the Pico's LEDWALL network
//  - says HELLO to the Pico every 0.5s so it knows this panel is alive
//  - reads the Pico's beacon: frame clock + layout (panel count, grid, who is in which slot)
//  - draws its own piece of the GIF, re-splitting automatically when panels join or leave
//  - flashes its slot number for 2s whenever the layout changes, so you know where to place it

#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "led_strip.h"
#include "gif_frames.h"

#define WIFI_SSID        "LEDWALL"
#define WIFI_PASS        "ledwall123"
#define PICO_IP          "192.168.4.1"
#define SYNC_PORT        4210
#define HELLO_PORT       4211
#define SYNC_MAGIC       0x4C454457
#define HELLO_MAGIC      0x48454C4F
#define PROTO_VERSION    2
#define FPS              30          // must match the Pico
#define MAX_NODES        8
#define LED_GPIO         14
#define PANEL_W          8
#define PANEL_H          8
#define LED_COUNT        (PANEL_W * PANEL_H)
#define BRIGHT           8           // 0-255 brightness cap
#define MARKER_US        2000000     // show slot number for 2s after a layout change

// If the picture looks scrambled (every other row reversed), set this to 1
#define MATRIX_SERPENTINE 0

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    uint32_t seq;
    uint8_t  cmd;
    uint32_t frame_no;
    uint8_t  layout_ver;
    uint8_t  count;
    uint8_t  cols;
    uint8_t  rows;
    uint32_t ids[MAX_NODES];
} beacon_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t id;
} hello_t;

static const char *TAG = "panel";
static uint32_t my_id;

// Shared between the receive task and the display loop
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t last_frame = 0;
static int64_t  last_rx_us = 0;
static bool     synced = false;
static int      my_slot = -1;        // -1 = Pico hasn't listed us yet
static int      cur_count = 0, cur_cols = 1, cur_rows = 1;
static int      cur_layout_ver = -1;

/* ---------- WiFi ---------- */
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "lost LEDWALL, retrying");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "joined LEDWALL, IP " IPSTR, IP2STR(&e->ip_info.ip));
    }
}

static void wifi_init(void)
{
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        r = nvs_flash_init();
    }
    ESP_ERROR_CHECK(r);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL);

    wifi_config_t wc = { 0 };
    memcpy(wc.sta.ssid, WIFI_SSID, strlen(WIFI_SSID));
    memcpy(wc.sta.password, WIFI_PASS, strlen(WIFI_PASS));
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);   // no radio sleep, or sync jitters by 100ms+
}

/* ---------- HELLO heartbeat to the Pico ---------- */
static void hello_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in to = {
        .sin_family = AF_INET,
        .sin_port = htons(HELLO_PORT),
    };
    to.sin_addr.s_addr = inet_addr(PICO_IP);

    hello_t h = { .magic = HELLO_MAGIC, .id = my_id };
    while (1) {
        sendto(sock, &h, sizeof(h), 0, (struct sockaddr *)&to, sizeof(to));  // fails harmlessly until WiFi is up
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

/* ---------- beacon receiver ---------- */
static void sync_rx_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(SYNC_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    uint32_t expected = 0, drops = 0;
    bool first = true;

    while (1) {
        beacon_t b;
        int n = recv(sock, &b, sizeof(b), 0);
        if (n != sizeof(b) || b.magic != SYNC_MAGIC || b.version != PROTO_VERSION) continue;

        int64_t now = esp_timer_get_time();
        if (!first && b.seq > expected) drops += b.seq - expected;
        first = false;
        expected = b.seq + 1;

        int count = b.count > MAX_NODES ? MAX_NODES : b.count;
        int slot = -1;
        for (int i = 0; i < count; i++)
            if (b.ids[i] == my_id) { slot = i; break; }

        taskENTER_CRITICAL(&lock);
        last_frame = b.frame_no;
        last_rx_us = now;
        synced = true;
        my_slot = slot;
        cur_count = count;
        cur_cols = b.cols ? b.cols : 1;
        cur_rows = b.rows ? b.rows : 1;
        cur_layout_ver = b.layout_ver;
        taskEXIT_CRITICAL(&lock);

        if (b.seq % 50 == 0)
            ESP_LOGI(TAG, "beacon seq=%" PRIu32 " frame=%" PRIu32 " drops=%" PRIu32 " panels=%d",
                     b.seq, b.frame_no, drops, count);
    }
}

/* ---------- drawing ---------- */
static led_strip_handle_t strip;

static inline uint8_t dim(uint8_t v) { return (uint16_t)v * BRIGHT / 255; }

static void px(int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
#if MATRIX_SERPENTINE
    if (y & 1) x = PANEL_W - 1 - x;
#endif
    led_strip_set_pixel(strip, y * PANEL_W + x, r, g, b);
}

static void clear_all(void)
{
    for (int i = 0; i < LED_COUNT; i++) led_strip_set_pixel(strip, i, 0, 0, 0);
}

// Draw this panel's piece of GIF frame idx.
// The wall is a (cols*8) x (rows*8) canvas. The GIF is scaled to COVER it (fill + crop),
// centred, and this panel samples the 8x8 block at its slot.
static void draw_gif(int idx, int slot, int cols, int rows)
{
    const float W = (float)(cols * PANEL_W);
    const float H = (float)(rows * PANEL_H);
    const float s = fmaxf(W / GIF_W, H / GIF_H);           // cover scale
    const float ox = (GIF_W * s - W) / 2.0f;               // crop offsets (centre)
    const float oy = (GIF_H * s - H) / 2.0f;
    const int col = slot % cols;
    const int row = slot / cols;

    for (int y = 0; y < PANEL_H; y++) {
        for (int x = 0; x < PANEL_W; x++) {
            float cx = col * PANEL_W + x + 0.5f;           // pixel centre on the canvas
            float cy = row * PANEL_H + y + 0.5f;
            int sx = (int)((cx + ox) / s);
            int sy = (int)((cy + oy) / s);
            if (sx < 0) sx = 0;
            if (sx >= GIF_W) sx = GIF_W - 1;
            if (sy < 0) sy = 0;
            if (sy >= GIF_H) sy = GIF_H - 1;
            const uint8_t *c = gif_frames[idx][sy][sx];
            px(x, y, dim(c[0]), dim(c[1]), dim(c[2]));
        }
    }
}

// 3x5 digits, bit 2 = left column
static const uint8_t font3x5[10][5] = {
    {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
    {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7},
};

// Slot marker: big digit (slot number, starting at 1) inside a coloured frame
static void draw_slot_number(int number)
{
    clear_all();
    uint8_t fr = dim(0), fg = dim(120), fb = dim(255);        // blue frame
    for (int i = 0; i < PANEL_W; i++) {
        px(i, 0, fr, fg, fb);
        px(i, PANEL_H - 1, fr, fg, fb);
        px(0, i, fr, fg, fb);
        px(PANEL_W - 1, i, fr, fg, fb);
    }
    const uint8_t *g = font3x5[number % 10];
    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 3; x++)
            if (g[y] & (4 >> x)) px(3 + x, 1 + y, dim(255), dim(255), dim(255));
}

void app_main(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    my_id = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) | mac[5];
    ESP_LOGI(TAG, "panel id %08" PRIx32, my_id);

    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_COUNT,
        .led_model = LED_MODEL_WS2812,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &strip));

    wifi_init();
    xTaskCreate(sync_rx_task, "sync_rx", 4096, NULL, 5, NULL);
    xTaskCreate(hello_task, "hello", 3072, NULL, 4, NULL);

    ESP_LOGI(TAG, "GIF: %d frames, %dx%d, %d fps", GIF_FRAMES, GIF_W, GIF_H, GIF_FPS);

    int     shown_slot = -2, shown_cols = 0, shown_rows = 0;
    int64_t marker_until = 0;

    while (1) {
        taskENTER_CRITICAL(&lock);
        uint32_t f0    = last_frame;
        int64_t  t0    = last_rx_us;
        bool     ok    = synced;
        int      slot  = my_slot;
        int      count = cur_count;
        int      cols  = cur_cols;
        int      rows  = cur_rows;
        int      lv    = cur_layout_ver;
        taskEXIT_CRITICAL(&lock);

        int64_t now = esp_timer_get_time();

        if (!ok) {
            // blinking red = no beacon from the Pico yet
            clear_all();
            if ((now / 500000) % 2) px(0, 0, BRIGHT, 0, 0);
        } else if (slot < 0) {
            // blinking blue = hearing the Pico, waiting to be given a slot
            clear_all();
            if ((now / 250000) % 2) px(0, 0, 0, 0, BRIGHT);
        } else {
            // Only show the number when THIS panel's slot or the grid shape changes,
            // not every time some other panel blips in and out.
            if (slot != shown_slot || cols != shown_cols || rows != shown_rows) {
                shown_slot = slot; shown_cols = cols; shown_rows = rows;
                marker_until = now + MARKER_US;
                ESP_LOGI(TAG, "layout v%d: %d panel(s) in %dx%d, I am slot %d (row %d, col %d)",
                         lv, count, cols, rows, slot + 1, slot / cols + 1, slot % cols + 1);
            }

            if (now < marker_until) {
                draw_slot_number(slot + 1);
            } else {
                uint64_t frame = f0 + (uint64_t)(now - t0) * FPS / 1000000;
                int idx = (int)((frame * GIF_FPS / FPS) % GIF_FRAMES);
                draw_gif(idx, slot, cols, rows);
            }
            if (now - t0 > 1000000) px(0, 0, BRIGHT, 0, 0);   // red corner = lost the Pico
        }

        led_strip_refresh(strip);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
