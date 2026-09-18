#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "esp_now_sync.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "espnow_sync";

static lm_frame_received_cb_t s_recv_cb = NULL;
static lm_position_t s_own_position = LM_POS_COUNT;

#if CONFIG_LM_ROLE_CONTROLLER
static uint8_t s_peer_mac[LM_POS_COUNT][6];
static bool    s_peer_valid[LM_POS_COUNT];
#endif

static bool parse_mac(const char *str, uint8_t mac[6])
{
    unsigned int b[6];
    if (sscanf(str, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        mac[i] = (uint8_t)b[i];
    }
    return true;
}

static void on_data_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (s_recv_cb == NULL || len != (int)sizeof(lm_frame_packet_t)) {
        return;
    }
    const lm_frame_packet_t *packet = (const lm_frame_packet_t *)data;
    if (packet->quadrant_id != (uint8_t)s_own_position) {
        ESP_LOGW(TAG, "Dropping packet for quadrant %d (this board is %d)",
                 packet->quadrant_id, s_own_position);
        return;
    }
    s_recv_cb(packet);
}

void lm_espnow_init(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LM_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
    // Modem sleep adds tens of ms of latency to ESP-NOW delivery -- disable
    // it so frame packets don't blow the 16.7ms (60fps) budget.
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_data_recv));
}

void lm_espnow_register_peers(void)
{
#if CONFIG_LM_ROLE_CONTROLLER
    static const char *kMacStrings[LM_POS_COUNT] = {
        CONFIG_LM_PEER_TOP_LEFT_MAC,
        CONFIG_LM_PEER_TOP_RIGHT_MAC,
        CONFIG_LM_PEER_BOTTOM_LEFT_MAC,
        CONFIG_LM_PEER_BOTTOM_RIGHT_MAC,
    };

    for (int pos = 0; pos < LM_POS_COUNT; pos++) {
        s_peer_valid[pos] = parse_mac(kMacStrings[pos], s_peer_mac[pos]);
        if (!s_peer_valid[pos]) {
            ESP_LOGE(TAG, "Invalid MAC for position %d: %s", pos, kMacStrings[pos]);
            continue;
        }
        esp_now_peer_info_t peer_info = {
            .channel = CONFIG_LM_ESPNOW_CHANNEL,
            .ifidx = WIFI_IF_STA,
            .encrypt = false,
        };
        memcpy(peer_info.peer_addr, s_peer_mac[pos], 6);
        ESP_ERROR_CHECK(esp_now_add_peer(&peer_info));
        ESP_LOGI(TAG, "Registered peer for position %d: %s", pos, kMacStrings[pos]);
    }
#endif
}

void lm_espnow_send_quadrant(lm_position_t position, uint32_t frame_seq, const uint8_t *pixels)
{
#if CONFIG_LM_ROLE_CONTROLLER
    if (position >= LM_POS_COUNT || !s_peer_valid[position]) {
        return;
    }
    lm_frame_packet_t packet = {
        .frame_seq = frame_seq,
        .quadrant_id = (uint8_t)position,
    };
    memcpy(packet.pixels, pixels, LM_QUADRANT_BYTES);
    esp_err_t err = esp_now_send(s_peer_mac[position], (const uint8_t *)&packet, sizeof(packet));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_now_send to position %d failed: %s", position, esp_err_to_name(err));
    }
#endif
}

void lm_espnow_set_receive_handler(lm_position_t own_position, lm_frame_received_cb_t cb)
{
    s_own_position = own_position;
    s_recv_cb = cb;
}
