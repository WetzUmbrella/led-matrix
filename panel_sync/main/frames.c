// Frame source: validated frames.bin from the "frames" partition, else built-in GIF.
// See frames.h for the file format.
#include <string.h>
#include "frames.h"

/* ---------- pure parsing (host-testable) ---------- */
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

uint32_t frames_crc32(const uint8_t *buf, size_t len)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        c ^= buf[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1u));
    }
    return ~c;
}

const char *frames_parse(const uint8_t *buf, size_t len, frame_set_t *out)
{
    if (len < FRAMES_HEADER_SIZE)                       return "too small for a header";
    if (memcmp(buf, FRAMES_MAGIC, 4) != 0)              return "no LEDA magic (partition empty?)";
    if (buf[4] != FRAMES_VERSION)                       return "unsupported version";
    int count = rd16(buf + 6), w = rd16(buf + 8), h = rd16(buf + 10), fps = rd16(buf + 12);
    if (count == 0 || w == 0 || h == 0)                 return "zero frames or zero size";
    if (fps == 0 || fps > 240)                          return "bad fps";
    size_t pix = (size_t)count * w * h * 3;
    if (pix > len - FRAMES_HEADER_SIZE)                 return "file shorter than header says";
    if (frames_crc32(buf + FRAMES_HEADER_SIZE, pix) != rd32(buf + 16)) return "CRC mismatch (corrupt data)";
    *out = (frame_set_t){ .w = w, .h = h, .count = count, .fps = fps,
                          .data = buf + FRAMES_HEADER_SIZE, .source = "partition" };
    return NULL;
}

/* ---------- ESP32 side ---------- */
#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_partition.h"
#include "gif_frames.h"

static const char *TAG = "frames";

static void use_builtin(frame_set_t *out, const char *why)
{
    ESP_LOGW(TAG, "using built-in animation: %s", why);
    *out = (frame_set_t){ .w = GIF_W, .h = GIF_H, .count = GIF_FRAMES, .fps = GIF_FPS,
                          .data = &gif_frames[0][0][0][0], .source = "built-in" };
}

void frames_load(frame_set_t *out)
{
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                        ESP_PARTITION_SUBTYPE_ANY, "frames");
    if (!p) { use_builtin(out, "no 'frames' partition in partition table"); return; }

    // Read just the header first so we only map as much flash as the file needs.
    uint8_t hdr[FRAMES_HEADER_SIZE];
    if (esp_partition_read(p, 0, hdr, sizeof hdr) != ESP_OK) { use_builtin(out, "flash read failed"); return; }
    if (memcmp(hdr, FRAMES_MAGIC, 4) != 0) { use_builtin(out, "no LEDA magic (partition empty?)"); return; }

    size_t need = FRAMES_HEADER_SIZE + (size_t)rd16(hdr + 6) * rd16(hdr + 8) * rd16(hdr + 10) * 3;
    if (need > p->size) { use_builtin(out, "header claims more data than the partition holds"); return; }

    const void *map; esp_partition_mmap_handle_t h;
    if (esp_partition_mmap(p, 0, need, ESP_PARTITION_MMAP_DATA, &map, &h) != ESP_OK) {
        use_builtin(out, "mmap failed"); return;
    }
    const char *err = frames_parse(map, need, out);
    if (err) { esp_partition_munmap(h); use_builtin(out, err); return; }
    // Mapping stays open for the life of the program; the renderer reads through it.
}
#endif
