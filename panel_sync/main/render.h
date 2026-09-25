// Display Rendering module (node output side)
// Owns everything between "what should this panel show" and the LEDs:
//   LED strip driver (RMT), brightness cap, pixel mapping, GIF crop/scale,
//   status indicators (no-sync / no-slot / lost-Pico) and the slot-number marker.
//
// The network side never touches the LEDs. It only answers one question,
// "what's the current state?", through the callback passed to render_start().
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// Snapshot of the network state the renderer needs, copied once per refresh.
typedef struct {
    bool     synced;       // heard at least one beacon from the Pico
    int      slot;         // this panel's slot in the grid, -1 = not assigned yet
    int      count;        // number of panels in the layout (for logging)
    int      cols, rows;   // grid shape, both >= 1
    int      layout_ver;   // layout version from the Pico (for logging)
    uint32_t frame_no;     // frame number carried by the last beacon
    int64_t  frame_rx_us;  // esp_timer_get_time() when that beacon arrived
} render_state_t;

// Fills *out with a consistent snapshot. Called from the render task every refresh,
// so it must be quick and must not block (a short critical section is fine).
typedef void (*render_state_fn)(render_state_t *out);

// Sets up the LED strip and starts the render task.
// master_fps is the Pico's frame clock rate (FPS in panel_sync.c).
esp_err_t render_start(render_state_fn get_state, int master_fps);
