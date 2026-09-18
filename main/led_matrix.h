#pragma once

#include "led_strip.h"

#define LM_MATRIX_WIDTH  8
#define LM_MATRIX_HEIGHT 8
#define LM_MATRIX_LEDS   (LM_MATRIX_WIDTH * LM_MATRIX_HEIGHT)

// Initializes the onboard 8x8 WS2812 matrix on the given GPIO and returns
// a ready-to-use handle (already cleared).
led_strip_handle_t lm_led_matrix_init(int gpio_num);

// Applies a full 8x8 frame in one shot: `pixels` is LM_MATRIX_LEDS RGB
// triplets in local raster order (index = y * LM_MATRIX_WIDTH + x),
// already brightness-scaled by the caller.
void lm_led_matrix_show(led_strip_handle_t strip, const uint8_t *pixels);
