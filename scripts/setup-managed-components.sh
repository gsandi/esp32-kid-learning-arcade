#!/usr/bin/env bash
# Copy managed_components from Elecrow reference repo into kid_arcade project.
# Run from any directory. Safe to re-run (nukes and rebuilds each time).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SRC_DIR="/Users/sandi/workspace/startup/7inch_9inch_10inch_P4_HMI_AI/managed_components"
DEST_DIR="$PROJECT_DIR/managed_components"

COMPONENTS=(
    espressif__esp_lcd_ek79007
    espressif__esp_lcd_touch
    espressif__esp_lcd_touch_gt911
    espressif__esp_lvgl_port
    lvgl__lvgl
)

echo "Source : $SRC_DIR"
echo "Dest   : $DEST_DIR"
echo ""

if [ ! -d "$SRC_DIR" ]; then
    echo "ERROR: Elecrow source repo not found at $SRC_DIR"
    exit 1
fi

rm -rf "$DEST_DIR"
mkdir -p "$DEST_DIR"

for comp in "${COMPONENTS[@]}"; do
    echo "Copying $comp ..."
    cp -r "$SRC_DIR/$comp" "$DEST_DIR/$comp"
done

echo ""
echo "Done. Contents of managed_components/:"
ls "$DEST_DIR"
