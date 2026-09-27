#!/bin/bash
# Convert a GIF and write it into the panel's "frames" partition. No rebuild, no app reflash.
# Usage (from panel_sync/, ESP-IDF loaded):
#   tools/upload_frames.sh mygif.gif [--size 16] [--fps 20]
# Set PORT if your board isn't /dev/ttyACM0.
set -e
PORT=${PORT:-/dev/ttyACM0}
python3 tools/make_frames.py "$@"
parttool.py --port "$PORT" write_partition --partition-name=frames --input=assets/frames.bin
echo "Done. Press RESET on the panel; the log should say '(partition)'."
