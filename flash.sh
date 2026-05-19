#!/usr/bin/env bash
# flash.sh — build + flash P4 firmware + flash C6 slave firmware in one shot
set -e

PORT="${1:-/dev/cu.wchusbserial10}"
C6_FW="c6_slave_fw/network_adapter.bin"
SLAVE_FW_OFFSET="0x10000"

echo "==> Building..."
pio run

echo ""
echo "==> Flashing P4 firmware (port: $PORT)..."
pio run -t upload --upload-port "$PORT"

echo ""
if [ ! -f "$C6_FW" ]; then
  echo "WARNING: $C6_FW not found — skipping C6 flash"
  exit 0
fi

echo "==> Flashing C6 slave firmware to slave_fw partition..."
python -m esptool \
  --chip esp32p4 \
  --port "$PORT" \
  -b 460800 \
  --before default-reset \
  --after hard-reset \
  write_flash \
  "$SLAVE_FW_OFFSET" "$C6_FW"

echo ""
echo "==> Done. Power-cycle or reset the board."
echo "    First boot: P4 streams C6 firmware via SDIO (~30s), then restarts."
echo "    Second boot: WiFi scan works normally."
