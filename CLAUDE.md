# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository status

This repo currently contains only planning docs — no source code, no `platformio.ini`, no build system yet. The authoritative spec is `esp32-learning-arcade-plan.md`. Read it before any implementation work; it defines scope, milestones, screens, data shapes, and the build order.

`changelog-claude.md` is appended to with a short entry per Claude Code session (timestamp, prompt, work done). Keep adding entries there.

## Project

A kid-friendly offline touchscreen learning toy for a 6-year-old. Two games (Math: count-and-pick; Spelling: starts-with-letter), 5 questions per round, stars as the only reward, no penalties/timers. Target hardware is a Hosyond 4.0" 320x480 ST7796S touchscreen on ESP32-32E.

## Target stack

- C++ on Arduino / **PlatformIO** (board: `esp32dev`)
- **LVGL** for UI
- **TFT_eSPI** or **LovyanGFX** for the ST7796S display
- **XPT2046_Touchscreen** (or LovyanGFX's built-in XPT2046 driver) — resistive, needs calibration on first boot
- **Preferences** (NVS) for persistent star count; LittleFS later if needed
- Offline-first; Wi-Fi/audio are explicitly out of scope for v1

When the project is bootstrapped, follow the file layout in the plan (`src/main.cpp`, `AppState.*`, `MathGame.*`, `SpellingGame.*`, `ProgressStore.*`, `UI.*`). The plan also says it's fine to start everything in `main.cpp` and split later — don't over-structure v1.

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

## Build order (do not skip ahead)

The plan specifies a strict order; the display/touch bring-up gate matters because ST7796S boards routinely ship with wrong driver/pin/rotation/touch-mapping defaults.

1. Display + touch bring-up (Milestone 1) — must be stable before any game work
2. Home screen with two buttons
3. Math game with hardcoded questions
4. Feedback screen
5. Spelling game with hardcoded questions
6. Round complete screen
7. Persist stars via `Preferences` (`learning` namespace, `stars` key)
8. Polish

## Design constraints to respect

- 320x480 portrait. Min touch target 60 px; answer buttons 60–80 px tall, 90–120 px wide.
- No emoji in v1 — embedded fonts won't render them. Use drawn shapes/letters.
- Wrong-answer screen must be soft and encouraging — no red flashes, buzzers, or timers.
- `QUESTIONS_PER_ROUND = 5`. Wrong answers cost nothing; only correct answers add a star.
- Question banks are hardcoded C arrays in v1 (`MathQuestion[]`, `SpellingQuestion[]`); no JSON/SD-card loading yet.

## Common PlatformIO commands (once `platformio.ini` exists)

```
pio run                  # build
pio run -t upload        # flash
pio device monitor       # serial monitor (use for touch-coordinate debugging)
pio run -t clean
```
