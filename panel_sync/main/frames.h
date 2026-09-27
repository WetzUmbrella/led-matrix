// Frame source for the renderer.
//
// Animations live in their own flash partition ("frames") as a single frames.bin,
// separate from the program. Changing the animation = rewrite that partition,
// no rebuild. The partition is memory-mapped, so frames are read straight from
// flash: no RAM copy, no filesystem.
//
// If the partition is empty or fails any check, the built-in gif_frames.h is
// used instead, so the wall never goes dark.
//
// frames.bin layout (little-endian), version 1:
//   0  char[4] magic   "LEDA"
//   4  u8      version  1
//   5  u8      reserved 0
//   6  u16     count    number of frames
//   8  u16     w        frame width  in pixels
//  10  u16     h        frame height in pixels
//  12  u16     fps      playback rate
//  14  u16     reserved 0
//  16  u32     crc32    CRC-32 (zlib/IEEE) of the pixel data below
//  20  pixels  count * h * w * 3 bytes, RGB, row-major, frame after frame
#pragma once
#include <stddef.h>
#include <stdint.h>

#define FRAMES_MAGIC       "LEDA"
#define FRAMES_VERSION     1
#define FRAMES_HEADER_SIZE 20

typedef struct {
    int            w, h, count, fps;
    const uint8_t *data;       // count * h * w * 3 bytes
    const char    *source;     // "partition" or "built-in", for logs
} frame_set_t;

// Pixel (x, y) of frame idx, 3 bytes RGB.
static inline const uint8_t *frame_px(const frame_set_t *fs, int idx, int x, int y)
{
    return fs->data + (((size_t)idx * fs->h + y) * fs->w + x) * 3;
}

// Checks a frames.bin image already in memory. On success fills *out
// (data points into buf) and returns NULL; on failure returns why.
// No ESP-IDF calls, so it can be unit-tested on a PC.
const char *frames_parse(const uint8_t *buf, size_t len, frame_set_t *out);

// CRC-32 (same as Python's zlib.crc32).
uint32_t frames_crc32(const uint8_t *buf, size_t len);

// Loads the animation: "frames" partition if valid, else built-in. Never fails.
void frames_load(frame_set_t *out);
