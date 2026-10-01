#!/bin/bash
# Give one panel board its permanent number. It's the board's router name (ESP-<number>)
# AND its fixed position in the wall (ESP-3 is always slot 3, whatever order boards power up in).
# Only needed once per board: it lives in the NVS partition, which `idf.py flash` doesn't touch.
# Usage (after get_idf):  ./set_board_number.sh <1-99> [port]     e.g. ./set_board_number.sh 2
set -e
N=$1
PORT=${2:-/dev/ttyACM0}
if ! [[ "$N" =~ ^[0-9]+$ ]] || [ "$N" -lt 1 ] || [ "$N" -gt 99 ]; then
    echo "usage: $0 <1-99> [port]"; exit 1
fi
: "${IDF_PATH:?run get_idf first}"

tmp=$(mktemp -d)
printf 'key,type,encoding,value\nboard,namespace,,\nnumber,data,u8,%s\n' "$N" > "$tmp/nvs.csv"
python "$IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py" \
    generate "$tmp/nvs.csv" "$tmp/nvs.bin" 0x6000 >/dev/null
esptool.py --chip esp32s3 -p "$PORT" write_flash 0x9000 "$tmp/nvs.bin"   # nvs partition (see partition table)
rm -rf "$tmp"
echo "This board is now ESP-$N"
