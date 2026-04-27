# ESP32 Kid Learning Arcade

A pocket-sized **offline** learning toy for kindergarten and pre-first-grade kids — Math (counting, missing numbers, ten-frame, addition under 10, make-10) and Reading (starts-with letter, missing letter in CVC words, rhyming, upper/lowercase match). Five questions per round, stars as the only reward, no penalties, no timers. **No internet, no accounts, no ads.**

> Built so my 6-year-old has something to play with on long car rides without me handing over a tablet.

<p align="center">
  <img src="assets/PXL_20260425_230540094.jpg" width="45%" alt="Home screen showing Math Game and Reading Game buttons"/>
  &nbsp;&nbsp;
  <img src="assets/PXL_20260425_230835775.jpg" width="45%" alt="Round complete screen showing 5 stars and Total: 30 stars"/>
</p>

---

## Who this is for

A parent or hobbyist who wants to build a simple offline learning toy for their kid in an afternoon. **No prior coding or electronics experience required** — if you can copy-paste a few lines into the Terminal app on your Mac, you can build one.

- **Time:** ~30 minutes, most of it automated software installs the first time.
- **Cost:** ~$25 in parts.
- **Soldering:** none. The screen ships pre-attached.

---

## What you'll need

| Item | Approx. price | Notes |
|---|---|---|
| 4.0" ESP32-32E touchscreen display | ~$25 | **US:** [search Amazon for "Hosyond ESP32 4 inch display"](https://www.amazon.com/s?k=hosyond+esp32+4+inch+display) (same hardware is also sold as "LCDWiki ESP32-32E"). The listing must say **320×480 ST7796S** with **USB-C and CH340C**. Manufacturer spec: [lcdwiki.com](http://www.lcdwiki.com/4.0inch_ESP32-32E_Display). |
| USB-C **data** cable | $0–$10 | Many USB-C cables are charge-only and won't enumerate as a serial device. If your laptop doesn't see the board after plugging in, swap cables before debugging anything else. Some boards include one. |
| A computer (Mac, Windows, or Linux) | — | Only needed once for flashing. After that the device runs standalone on USB power (any phone charger works). |

---

## Setup on macOS — the easy path

This is the recommended path if you're on a MacBook.

### Step 1 — Open Terminal

On your Mac, press **⌘ + Space** to open Spotlight, type `Terminal`, and press **Return**. A window with a text prompt will open. Everything below happens here — just paste each command and press Return.

### Step 2 — Download the project

Paste this and press Return:

```bash
cd ~/Desktop && git clone https://github.com/gsandi/esp32-kid-learning-arcade.git && cd esp32-kid-learning-arcade
```

> **First-time only:** macOS may pop up a dialog asking to install "Command Line Tools for Xcode". Click **Install** and wait 5–10 minutes for it to finish, then re-run the line above. This gives you `git` and the basic compilers.

### Step 3 — Run the setup script

```bash
./scripts/setup-macos.sh
```

This installs **Homebrew** (the Mac package manager) and **PlatformIO** (the embedded build system). The first run takes ~5 minutes; macOS may ask for your account password partway through — that's normal.

The script is safe to re-run; anything already installed is skipped.

### Step 4 — Plug in the board and flash

Plug the ESP32 into your Mac with the USB-C cable, then run:

```bash
pio run -t upload
```

PlatformIO auto-detects the board, builds the firmware, and flashes it. The device boots into the home screen the moment the upload finishes (~30 seconds).

### Step 5 — Calibrate the touch screen (required, ~3 minutes)

**Don't skip this step.** Every XPT2046 resistive touch panel reads slightly differently — until you calibrate, your taps will land in the wrong place. The four `RAW_X_LEFT / RAW_X_RIGHT / RAW_Y_TOP / RAW_Y_BOTTOM` constants in `src/main.cpp` are tuned to the panel I built on, not yours.

1. With the device still plugged into your Mac, open the serial monitor:

   ```bash
   pio device monitor
   ```

2. Tap each corner of the screen. Every tap prints a line like:

   ```
   TAP raw=(218,3712) mapped=(...) screen=0 admin=0
   ```

   Write down the `raw=(x,y)` numbers for all four corners — top-left, top-right, bottom-left, bottom-right.

3. Quit the monitor with **Ctrl + C**. Then open `src/main.cpp` in any text editor (TextEdit works — drag the file onto TextEdit's icon, then choose **Format → Make Plain Text** if it complains).

4. Find these four lines near the top of the file and update the numbers with what you wrote down:

   ```cpp
   constexpr uint16_t RAW_X_LEFT   =  223;   // raw X from a leftmost-edge tap
   constexpr uint16_t RAW_X_RIGHT  = 3761;   // raw X from a rightmost-edge tap
   constexpr uint16_t RAW_Y_TOP    =  ...;   // raw Y from a top-edge tap
   constexpr uint16_t RAW_Y_BOTTOM =  ...;   // raw Y from a bottom-edge tap
   ```

5. Save the file, then re-flash:

   ```bash
   pio run -t upload
   ```

6. Tap the home-screen buttons — taps should now land where your finger actually is.

**If your taps register on the wrong side** (tapping left registers as right, or top registers as bottom), the axis is mirrored. Just **swap the two values** within the affected pair (`RAW_X_LEFT` ↔ `RAW_X_RIGHT`, or `RAW_Y_TOP` ↔ `RAW_Y_BOTTOM`) and re-flash. No other code change needed.

### Done

Unplug the device from your Mac and plug it into any phone charger — it now runs standalone.

---

## Setup on Windows or Linux

<details>
<summary>Click to expand</summary>

### Windows

1. Install [PlatformIO IDE for VS Code](https://platformio.org/install/ide?install=vscode).
2. Open the project folder in VS Code.
3. Plug in the board. Windows usually picks up the CH340 driver automatically; if it doesn't, grab it from [WCH](https://www.wch-ic.com/downloads/CH341SER_ZIP.html).
4. Click the PlatformIO **upload** button (right-arrow icon in the bottom toolbar).

### Linux

```bash
pip install --user platformio
sudo usermod -aG dialout $USER   # then log out and back in
git clone https://github.com/gsandi/esp32-kid-learning-arcade.git
cd esp32-kid-learning-arcade
pio run -t upload
```

</details>

---

## Personalize before flashing

All personalization lives in `src/main.cpp`. Three things you'll almost certainly want to change:

| What | Where | Default |
|---|---|---|
| **Kid's name** on the home screen | `tft.drawString("Dhruv", ...)` (search for `"Dhruv"`) | `"Dhruv"` |
| **Admin PIN** for resetting stars | `const char* const ADMIN_PIN = "...";` | `"0000"` — **change this!** |
| **Questions per round** | `constexpr int QUESTIONS_PER_ROUND` | `5` |

The question banks are also in `src/main.cpp` as plain C arrays. Search for any of:

```
countBank, missingNumBank, addBank, make10Bank, tenFrameBank,
startsWithBank, missingLetterBank, rhymeBank, upperLowerBank
```

Adding a question is one line of code per bank. Re-flash with `pio run -t upload` to pick up the changes.

---

## Using the device

- **Idle screensaver** — after 60 seconds with no touch the screen goes dark with a floating colour-particle animation. Tap anywhere to wake; if the lock is enabled (see Admin panel) it goes to a PIN screen instead of home.
- **Tap Math or Reading** on the home screen to start a 5-question round.
- **Tap an answer.** Right answer → "Great job!" + a star is earned. Wrong answer → "Try again!" + retry the same question (no penalty).
- After 5 right answers the round-complete screen shows total stars; tap **Play Again** to start a fresh round in the same game.
- Stars persist across power cycles (stored in ESP32 NVS via the `Preferences` library).

### Admin panel (parent only)

1. From the home screen, **press and hold the bottom-right corner for ~2 seconds.** (The corner is unmarked — kids won't stumble on it.)
2. Enter your admin PIN (default `0000` — change it in `main.cpp` before flashing).
3. The admin screen has four controls:

| Button | What it does |
|---|---|
| **Reset Stars** | Zeroes the star count and returns to home |
| **Lock: ON / OFF** | When ON, device requires the PIN after screensaver or power-on — good for shared devices |
| **Math: Easy / Hard** | Easy = counting, ten-frame, addition; Hard = all five types including missing-number and make-10 |
| **Reading: Easy / Hard** | Easy = starts-with and uppercase/lowercase; Hard = all four types including rhyme and missing-letter |

All settings survive power cycles.

---

## Troubleshooting

| Symptom | Likely cause + fix |
|---|---|
| **macOS asks for your password during setup** | That's the Homebrew installer. Type your Mac account password and press Return — it's not sent anywhere. |
| **`zsh: command not found: pio`** after the setup script | Quit Terminal and open a fresh window. PATH changes only apply to new shells. |
| **PlatformIO can't find the board** | (1) USB-C cable is charge-only — try a different cable. (2) CH340 driver missing — install [WCH CH340](https://www.wch-ic.com/downloads/CH34XSER_MAC_ZIP.html) and reboot. (3) Multiple USB serial devices plugged in — set `upload_port` explicitly in `platformio.ini`. |
| **Display is white / blank** | Confirm `-DST7796_DRIVER=1` is in `platformio.ini`, not `ILI9486` or `ILI9488`. Some sellers ship the same physical board with different controllers. |
| **Touch is mirrored or upside-down** | Swap the values inside the `RAW_X_*` or `RAW_Y_*` pair in `main.cpp` (see Step 5 — Calibrate the touch screen). |
| **Letters render blank but numbers work** | You're using TFT_eSPI Font 6 — it's digits + punctuation only. Use Font 4 with `setTextSize(2)` for big readable text. |
| **Upload fails with "Connecting…" hang** | Hold the BOOT button on the back of the board, hit `pio run -t upload`, release BOOT once "Connecting…" prints. (Most CH340C boards auto-reset and don't need this — but if yours has a flaky USB-C jack, the manual dance helps.) |

---

## Customizing further

- **Add a new question type:** add an entry to the `QType` enum, a new `*Bank` array, a case in `bankSizeFor()`, a draw function, and an answer-check case. Roughly 30 lines per type.
- **Change the colors / theme:** all UI rendering is in `main.cpp`. Search for `TFT_PURPLE`, `TFT_YELLOW`, etc. to retheme.
- **Wi-Fi or audio:** explicitly out of scope for v1 — the goal is offline + silent so the toy doesn't compete with bells and whistles for the kid's attention. PRs welcome if you disagree.

---

## Project layout

```
esp32-kid-learning-arcade/
├── platformio.ini                   # Build config + TFT_eSPI pin flags
├── scripts/setup-macos.sh           # One-shot macOS setup (Homebrew + PlatformIO)
├── src/main.cpp                     # All firmware (~1400 lines, single file by design)
├── assets/                          # Photos used in this README
├── LICENSE
└── README.md
```

The firmware is intentionally a single file. It's easy to read top-to-bottom — splitting it across modules would make forking and tweaking harder, not easier, for the audience this is built for.

---

## Hardware reference

If you want to dig into pinouts or extend the firmware, the board's spec page is at [lcdwiki.com/4.0inch_ESP32-32E_Display](http://www.lcdwiki.com/4.0inch_ESP32-32E_Display). Key facts:

- ESP32-WROOM-32E, 4 MB flash, 520 KB SRAM
- Display + touch share SPI on GPIO 12/13/14
- Touch IRQ is on GPIO 36 (input-only, no internal pullup)

---

## License

[MIT](LICENSE) — free to use, modify, and **sell**. Build something for your kid, build a few extras for friends. If you make something cool, I'd love to see it.

---

## Credits

- Display driver: [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) by Bodmer
- Hardware: 4.0" ESP32-32E from [LCDWiki](http://www.lcdwiki.com/4.0inch_ESP32-32E_Display) / Hosyond
- Built with development assistance from [Claude Code](https://claude.com/claude-code)
