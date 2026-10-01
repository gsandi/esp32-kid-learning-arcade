#!/usr/bin/env bash
# ota_push.sh — build and serve firmware for wireless OTA
# Usage: ./ota_push.sh
#   On device: Admin -> OTA Update -> enter this machine's IP -> Start
set -e

echo "==> Building..."
pio run

echo "==> Baking OTA artwork..."
python3 tools/bake_art.py .pio/build/esp32p4/art.bin

BIN=".pio/build/esp32p4/firmware.bin"
if [ ! -f "$BIN" ]; then
    echo "ERROR: $BIN not found after build"
    exit 1
fi

SIZE=$(wc -c < "$BIN")
ART=".pio/build/esp32p4/art.bin"
ART_SIZE=$(wc -c < "$ART")
echo ""
echo "==> Firmware ready: $BIN ($SIZE bytes)"
echo "==> Artwork ready:  $ART ($ART_SIZE bytes)"
echo ""
echo "    Your IP addresses:"
ifconfig | grep "inet " | grep -v "127.0.0.1" | awk '{print "      " $2}'
echo ""
echo "==> Serving on port 8080... (Ctrl-C to stop)"
python3 -m http.server 8080 --directory .pio/build/esp32p4/
