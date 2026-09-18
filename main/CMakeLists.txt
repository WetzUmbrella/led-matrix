idf_component_register(SRCS "led-matrix.c" "led_matrix.c" "esp_now_sync.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_wifi nvs_flash esp_event esp_netif esp_timer)
