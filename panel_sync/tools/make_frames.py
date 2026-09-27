#!/usr/bin/env python3
"""
GIF -> frames.bin for the LED wall's "frames" flash partition.

Usage (from panel_sync/):
    python3 tools/make_frames.py mygif.gif                 # 32px, GIF's own speed
    python3 tools/make_frames.py mygif.gif --size 16 --fps 20

Writes assets/frames.bin (format in main/frames.h) and assets/preview.png.
Transparent areas become black; shape is kept (the panel crops to fill the wall).
"""
import argparse, os, struct, sys, zlib
from PIL import Image, ImageSequence

PARTITION_SIZE = 0x200000        # must match "frames" in partitions.csv
MASTER_FPS = 30                  # Pico frame clock; can't play faster than this
HEADER = struct.Struct("<4sBBHHHHHI")   # magic, ver, rsv, count, w, h, fps, rsv, crc32 = 20 bytes

ap = argparse.ArgumentParser()
ap.add_argument("gif")
ap.add_argument("--size", type=int, default=32, help="longest side in pixels (default 32)")
ap.add_argument("--fps", type=int, help="override playback fps")
ap.add_argument("--out", default="assets/frames.bin")
a = ap.parse_args()

img = Image.open(a.gif)
w0, h0 = img.size
k = min(1.0, a.size / max(w0, h0))   # shrink big GIFs, never blow up small ones
W, H = max(1, round(w0 * k)), max(1, round(h0 * k))

frames, durs = [], []
for fr in ImageSequence.Iterator(img):
    rgba = fr.convert("RGBA")
    rgb = Image.alpha_composite(Image.new("RGBA", rgba.size, (0, 0, 0, 255)), rgba).convert("RGB")
    frames.append(rgb.resize((W, H), Image.BOX))
    durs.append(fr.info.get("duration", img.info.get("duration", 100)) or 100)

fps = a.fps or max(1, round(1000 / (sum(durs) / len(durs))))
if fps > MASTER_FPS:
    print(f"note: GIF wants {fps} fps, Pico clock is {MASTER_FPS}; capping")
    fps = MASTER_FPS

pixels = b"".join(f.tobytes() for f in frames)
total = HEADER.size + len(pixels)
if total > PARTITION_SIZE:
    sys.exit(f"Too big: {total // 1024}KB, partition is {PARTITION_SIZE // 1024}KB. Use --size 16 or fewer frames.")

os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
with open(a.out, "wb") as f:
    f.write(HEADER.pack(b"LEDA", 1, 0, len(frames), W, H, fps, 0, zlib.crc32(pixels)))
    f.write(pixels)
frames[0].resize((W * 8, H * 8), Image.NEAREST).save(os.path.join(os.path.dirname(a.out) or ".", "preview.png"))
print(f"{len(frames)} frames, {W}x{H}, {fps} fps, {total // 1024}KB -> {a.out}")
