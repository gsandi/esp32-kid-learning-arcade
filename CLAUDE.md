# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository status

**Public** at `gsandi/esp32-kid-learning-arcade`. `main` is live and stable — only push tested, device-flashed code there.

`changelog-claude.md` is appended to with a short entry per Claude Code session (timestamp, prompt, work done). Keep adding entries there.

## Project

Offline touchscreen learning toy for a 6-year-old. Two games — **Math** (counting, missing numbers, ten-frame, add under 10, make-10, skip counting by 2s/3s) and **Reading** (starts-with letter, missing letter, rhyme, upper/lowercase). 5 questions per round, stars only reward, no penalties/timers. Hardware: Hosyond 4.0" 320×480 ST7796S on ESP32-32E.

## Stack (actual)

- C++ / **PlatformIO** (`esp32dev`)
- **TFT_eSPI** — display driver (ST7796S) and touch (XPT2046 via `TOUCH_CS`)
- **Preferences** (NVS) — persistent star count + settings (`learning` namespace)
- **ArduinoJson** + SD card — optional question bank loading (v2; gitignored until tested)
- Single-file firmware: `src/main.cpp`. No LVGL. Don't add abstraction layers unless splitting is unavoidable.

## Git workflow

Repo is public. `main` must always be stable and device-tested.

```
main          ← public, stable, flashed. Never push untested code here.
feat/<name>   ← new features (e.g. feat/skip-counting, feat/multiplication)
fix/<name>    ← bug fixes
```

**Rules:**
1. All new work goes on a branch — never commit experimental code directly to `main`.
2. Build passes (`pio run`) before merging.
3. Flash to device and verify before merging to `main`.
4. Stash or branch for in-progress work that isn't ready to commit.
5. Merge branch → `main` locally, then push. No force-push to `main`.

## Hardware: Hosyond / LCDWiki 4.0" ESP32-32E (E32R40T)

Verified from https://www.lcdwiki.com/4.0inch_ESP32-32E_Display:

- **MCU:** ESP32-WROOM-32E (ESP32-D0WD-V3), 4 MB QSPI flash, 520 KB SRAM
- **Display:** ST7796S, 320×480, 4-line SPI, RGB565
- **Touch:** XPT2046, **resistive**, SPI (shared bus with display)
- **USB:** Type-C with CH340C and auto-reset — no BOOT button dance. macOS needs the CH340 driver from WCH if `/dev/cu.wchusbserial*` doesn't appear.

Pinout (display + touch share the SPI bus on 12/13/14):

| Function       | GPIO |
|----------------|------|
| TFT_MOSI       | 13   |
| TFT_MISO       | 12   |
| TFT_SCLK       | 14   |
| TFT_CS         | 15   |
| TFT_DC         | 2    |
| TFT_RST        | -1 (tied to EN) |
| TFT_BL         | 27   |
| TOUCH_CS       | 33   |
| TOUCH_IRQ      | 36 (input-only, active low) |
| SD_CS          | 5    |
| SD_MOSI        | 23   |
| SD_MISO        | 19   |
| SD_SCLK        | 18   |

SD card is on a **separate** SPI bus (HSPI on 18/19/23) — don't try to share with the display.

GPIO 36 is input-only — fine for touch IRQ, but no internal pull-up; XPT2046 IRQ is open-drain so an external pull-up may be required if the board doesn't include one (verify with a multimeter before assuming).

### TFT_eSPI `User_Setup.h` (minimum)

```c
#define ST7796_DRIVER
#define TFT_WIDTH  320
#define TFT_HEIGHT 480
#define TFT_MOSI 13
#define TFT_MISO 12
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define TFT_BL   27
#define TOUCH_CS 33
#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define SMOOTH_FONT
```

In PlatformIO, prefer setting these via `build_flags` in `platformio.ini` rather than editing the library's `User_Setup.h` (which gets clobbered on dependency reinstall).

## Design constraints to respect

- 320x480 portrait. Min touch target 60 px; answer buttons 60–80 px tall, 90–120 px wide.
- No emoji in v1 — embedded fonts won't render them. Use drawn shapes/letters.
- Wrong-answer screen must be soft and encouraging — no red flashes, buzzers, or timers.
- `QUESTIONS_PER_ROUND = 5`. Wrong answers cost nothing; only correct answers add a star.
- Question banks are hardcoded C arrays (`CountQ[]`, `AddQ[]`, etc.) with optional SD-card override — SD loading is wired but gitignored until end-to-end tested.

## Common PlatformIO commands (once `platformio.ini` exists)

```
pio run                  # build
pio run -t upload        # flash
pio device monitor       # serial monitor (use for touch-coordinate debugging)
pio run -t clean
```
