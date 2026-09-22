#!/usr/bin/env bash
# flash.sh — build + flash P4 firmware + flash C6 slave firmware in one shot
set -e

PORT="${1:-/dev/cu.wchusbserial10}"

echo "==> Building..."
pio run

echo "==> Baking art partition..."
python3 tools/bake_art.py

echo ""
echo "==> Flashing P4 firmware (port: $PORT)..."
pio run -t upload --upload-port "$PORT"

echo ""
echo "==> Flashing art partition (dials + icons) at 0x950000..."
pio pkg exec --package tool-esptoolpy -- esptool.py \
  --chip esp32p4 \
  --port "$PORT" \
  -b 460800 \
  --before default_reset \
  --after hard_reset \
  write_flash \
  --force \
  0x950000 build/art.bin

echo ""
echo "==> Done. Power-cycle or reset the board."
echo "    The PlatformIO upload hook also refreshed the C6 slave_fw partition."
echo "    First boot: P4 streams C6 firmware via SDIO (~30s), then restarts."
echo "    Second boot: WiFi scan works normally."
