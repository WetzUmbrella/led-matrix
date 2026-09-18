#pragma once

#include <stdint.h>
#include "esp_now.h"

#define LM_QUADRANT_LEDS  64 // one 8x8 board's worth of pixels
#define LM_QUADRANT_BYTES (LM_QUADRANT_LEDS * 3)

typedef enum {
    LM_POS_TOP_LEFT = 0,
    LM_POS_TOP_RIGHT,
    LM_POS_BOTTOM_LEFT,
    LM_POS_BOTTOM_RIGHT,
    LM_POS_COUNT
} lm_position_t;

typedef struct __attribute__((packed)) {
    uint32_t frame_seq;
    uint8_t  quadrant_id;               // an lm_position_t value
    uint8_t  pixels[LM_QUADRANT_BYTES]; // RGB triplets, local raster order
} lm_frame_packet_t;

typedef void (*lm_frame_received_cb_t)(const lm_frame_packet_t *packet);

// Brings up NVS + WiFi STA (no AP association) + ESP-NOW on the configured
// channel, with power save disabled. Must be called once, before any
// send/receive.
void lm_espnow_init(void);

// Controller only: registers the 4 grid-position peer MACs from Kconfig
// (the entry for this board's own position is registered but unused).
void lm_espnow_register_peers(void);

// Controller only: unicasts one quadrant's frame to the peer at that grid
// position. No-op (logs a warning) if that position's MAC didn't parse.
void lm_espnow_send_quadrant(lm_position_t position, uint32_t frame_seq, const uint8_t *pixels);

// Peer only: registers the callback invoked whenever a packet arrives whose
// quadrant_id matches `own_position` (packets for other quadrants are
// dropped with a warning -- that would indicate a MAC/position mismatch in
// the controller's Kconfig).
void lm_espnow_set_receive_handler(lm_position_t own_position, lm_frame_received_cb_t cb);
