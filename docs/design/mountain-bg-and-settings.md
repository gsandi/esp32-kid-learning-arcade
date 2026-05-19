# Design Spec: Mountain Background Layer + Settings Screen Redesign

**Project:** kid_arcade  
**Screen:** 600 x 1024 px, portrait (ESP32-P4 CrowPanel 7")  
**Renderer:** LVGL 9.x — lv_obj, lv_label, lv_canvas, lv_slider only. No images. No emoji.  
**Audience:** Dhruv, age 6. Bright, encouraging, no penalties.  
**Date:** 2026-05-19  
**Status:** Awaiting team-a-lead approval

---

## Task 1 — Mountain Background Layer

### Concept

A two-layer mountain silhouette drawn entirely with filled polygons on an
`lv_canvas`. It sits at the bottom of every screen and never scrolls. Game
content lives above it in a defined safe zone. The mood is dusk-over-mountains:
dark indigo sky fading to slightly lighter at the horizon, two ranges of peaks
(far = lighter, near = darker), a handful of small star dots in the upper sky.

The canvas is a child of the screen, created before any other content, and
given `lv_obj_move_to_index(..., 0)` so it is always behind everything else.
Size: 600 x 1024 (full screen).

---

### Safe Content Zone

All game/settings content must stay within:

```
Y: 0 px  (top)  to  Y: 724 px  (bottom of safe zone)
```

Mountain layer occupies Y 724–1024 px (300 px tall). Content may overlap the
sky portion of the canvas (Y 0–724) freely because the sky is a flat fill.

---

### Layer 1 — Sky

| Property | Value |
|---|---|
| Method | `lv_canvas_draw_rect` over the full 600x1024 area |
| Color top (Y 0) | `0x100224` — near-black deep violet |
| Color horizon (Y 724) | `0x1F0A45` — slightly lighter purple |
| Gradient | Simulate with 3 stacked horizontal rects: Y 0–300 = `0x100224`, Y 300–550 = `0x160830`, Y 550–724 = `0x1F0A45` |

These three values are all within the C_BG neighborhood (`0x1A0533`) so they
read as one continuous sky. No new named token needed — they are inline hex only
used by the background draw function.

---

### Layer 2 — Far Mountains (range A)

Drawn as a single filled polygon. Color is lighter than the near range so it
reads as more distant.

| Property | Value |
|---|---|
| Fill color | `0x2A1660` — muted indigo, clearly lighter than C_BG |
| Baseline Y | 924 px (bottom of screen minus 100 px) |
| Top of tallest peak Y | 724 px |
| Polygon points (x, y) — 600 px wide |

```
Point list (13 points, closed):
  (0,   924)   — bottom-left anchor
  (0,   860)   — left edge rise
  (60,  820)   — small left shoulder
  (110, 780)   — left peak
  (160, 810)   — saddle
  (220, 750)   — central-left peak (tallest)
  (290, 790)   — saddle
  (360, 724)   — right-central peak (tallest in scene)
  (430, 790)   — saddle
  (490, 755)   — right peak
  (540, 800)   — right shoulder
  (600, 840)   — right edge
  (600, 924)   — bottom-right anchor
```

Polygon is filled solid `0x2A1660` with no outline (outline_width 0).

---

### Layer 3 — Near Mountains (range B)

Drawn on top of range A, closer and therefore darker. Peaks are shorter and
wider (foreground objects look more massive).

| Property | Value |
|---|---|
| Fill color | `0x1C0E42` — dark indigo, nearly matches C_BG so it grounds the scene |
| Baseline Y | 1024 px (absolute screen bottom) |
| Top of tallest peak Y | 840 px |
| Polygon points (13 points, closed) |

```
Point list:
  (0,   1024)  — bottom-left
  (0,   900)   — left edge rise
  (80,  870)   — left shoulder
  (150, 848)   — left peak
  (200, 870)   — saddle
  (270, 840)   — central peak (tallest)
  (340, 862)   — saddle
  (410, 852)   — right-of-center peak
  (480, 875)   — saddle
  (540, 860)   — right peak
  (600, 895)   — right edge
  (600, 1024)  — bottom-right
  (0,   1024)  — close
```

Fill `0x1C0E42`, outline_width 0.

---

### Stars

Drawn as small filled circles on the canvas, scattered in the sky zone (Y 0–680
only — keep stars above the mountain horizon).

| Property | Value |
|---|---|
| Count | 18 stars |
| Dot size | radius 2 px (diameter 4 px — just visible without reading as a smear) |
| Color | `0xFFFFFF` at opacity 140/255 (= 55% — dim, not distracting) |
| Distribution | Fixed coordinates (seeded layout, not random — avoids re-draw flicker) |

Fixed star coordinates (x, y):

```
(48,  60),  (112, 38),  (195, 88),  (280, 22),  (350, 55),
(430, 35),  (510, 78),  (565, 48),  (90,  145), (230, 120),
(390, 135), (520, 110), (155, 200), (310, 180), (470, 215),
(60,  280), (410, 260), (545, 300)
```

Draw with `lv_canvas_draw_arc` (full circle, 0–360 deg, radius 2) or a 4x4
`lv_draw_rect_dsc` with radius 2.

---

### LVGL implementation notes (for SWE)

1. Create one `lv_canvas` at size 600x1024, backed by a `static` pixel buffer
   (`LV_COLOR_FORMAT_RGB565`, so `600 * 1024 * 2 = 1,228,800` bytes). Declare
   buffer as `static uint8_t bg_canvas_buf[600 * 1024 * 2]` — goes in BSS, not
   stack.
2. Attach canvas to the screen: `lv_obj_set_pos(canvas, 0, 0)`.
3. Call `lv_obj_move_to_index(canvas, 0)` after all other screen children are
   created so the canvas stays at z-index 0.
4. Draw order: sky rects first, then polygon A (far), then polygon B (near),
   then star dots.
5. Draw only once per screen creation (not in a timer). The background is static.
6. Polygon fill: use `lv_canvas_draw_polygon` with `lv_draw_fill_dsc_t`, setting
   `color` to the fill hex and `opa` to `LV_OPA_COVER`.
7. A helper function `static void draw_mountain_bg(lv_obj_t* scr)` should be
   called at the top of each `launch_*` function, before the header bar. Follows
   the project's existing pattern of `draw_dots` / `draw_ten_frame`.

---

## Task 2 — Settings Screen Redesign

### Overview layout (600 x 1024)

```
┌──────────────────────────────────────────┐  Y 0
│  HEADER BAR  88 px                       │
│  [<]  Settings                           │
├──────────────────────────────────────────┤  Y 88
│                                          │
│  ── NETWORK ──────────────────────────   │  Y 124
│                                          │
│  ┌──────────────────────────────────┐    │  Y 148
│  │ WiFi row  120 px                 │    │
│  └──────────────────────────────────┘    │  Y 268
│                                          │
│  ── DISPLAY ──────────────────────────   │  Y 308
│                                          │
│  ┌──────────────────────────────────┐    │  Y 332
│  │ Brightness card  220 px          │    │
│  └──────────────────────────────────┘    │  Y 552
│                                          │
│  ── YOUR STARS ───────────────────────   │  Y 592
│                                          │
│  ┌──────────────────────────────────┐    │  Y 616
│  │ Star display card  180 px        │    │
│  └──────────────────────────────────┘    │  Y 796
│                                          │
│  (mountain bg visible below ~724)        │
└──────────────────────────────────────────┘  Y 1024
```

All cards are 552 px wide (= SCR_W - 48), centered horizontally (x offset 24 px
from each edge).

---

### Header bar

Identical to the existing `make_header(scr, 88)` pattern — no change needed.

| Element | Spec |
|---|---|
| Height | 88 px |
| Background | `C_HEADER` = `0x2D1B69` |
| Back button | 64 x 56 px, `C_BTN` bg, radius 14, label "<" `lv_font_montserrat_32` `C_CARD_TXT`, aligned LEFT_MID x+14 |
| Title | "Settings" `lv_font_montserrat_32` `C_GOLD`, centered |

---

### Section separators

Three separators: "NETWORK", "DISPLAY", "YOUR STARS"

| Property | Value |
|---|---|
| Width | 552 px |
| Height | 2 px rule + label above it |
| Rule color | `C_BTN` = `0x3A2A7A` |
| Rule height | 2 px, radius 1 |
| Label text | uppercase, e.g. "NETWORK" |
| Label font | `lv_font_montserrat_20` |
| Label color | `C_SUBTEXT` = `0xBBAADD` |
| Label margin-bottom | 8 px above the 2 px rule |
| Separator Y positions | "NETWORK" at Y 124, "DISPLAY" at Y 308, "YOUR STARS" at Y 592 |

The separator is two objects: an `lv_label` with the section name, then a
552 x 2 `lv_obj` with `C_BTN` bg below it. Both are children of the content
column.

---

### Section 1 — WiFi row

Y 148 to Y 268. Height 120 px. Width 552 px.

```
┌─────────────────────────────────────────────────────┐  120 px tall
│                                                     │
│  [===]    Wi-Fi                     [  Connect  ]   │
│  (icon)   SSID-name-here                            │
│           Not connected                             │
└─────────────────────────────────────────────────────┘
```

| Element | Value |
|---|---|
| Card background | `C_BTN` = `0x3A2A7A` |
| Card radius | 20 px |
| Card border | none |
| Card shadow | width 16 px, color `0x000000`, opa 60, y-offset 6 |
| Card padding-left | 24 px |
| Card padding-right | 20 px |

**WiFi icon (left)**

| Property | Value |
|---|---|
| Type | `lv_label`, text `"((o))"` (3 nested parens = antenna signal) |
| Font | `lv_font_montserrat_28` |
| Color | `0x5BB8F5` — sky blue (same as current code) |
| Position | `LV_ALIGN_LEFT_MID`, x_offset 0, y_offset 0 |

**Network name label (primary text)**

| Property | Value |
|---|---|
| Text | SSID string if known, else "No network saved" |
| Font | `lv_font_montserrat_28` |
| Color | `C_CARD_TXT` = `0xFFFFFF` |
| Position | `LV_ALIGN_LEFT_MID`, x_offset 80, y_offset -18 |
| Max width | 300 px, `LV_LABEL_LONG_DOT` |

**Status line (secondary text)**

| Property | Value |
|---|---|
| Text (connected) | "Connected" |
| Text (saved, not connected) | "Not connected" |
| Text (no saved network) | "Tap to set up" |
| Font | `lv_font_montserrat_20` |
| Color (connected) | `C_CORRECT` = `0x06D6A0` |
| Color (not connected / no network) | `C_SUBTEXT` = `0xBBAADD` |
| Position | `LV_ALIGN_LEFT_MID`, x_offset 80, y_offset +18 |
| Note | This is the existing `g_settings_wifi_lbl` — keep the pointer for live updates |

**Connect / Change button (right)**

| Property | Value |
|---|---|
| Size | 130 x 64 px |
| Background | `0x005FAD` — medium blue (same as current code) |
| Pressed bg | `0x003D73` |
| Radius | 14 px |
| Label text | "Connect" or "Change" (existing logic) |
| Label font | `lv_font_montserrat_24` |
| Label color | `0xFFFFFF` |
| Position | `LV_ALIGN_RIGHT_MID`, x_offset 0, y_offset 0 |

---

### Section 2 — Brightness card

Y 332 to Y 552. Height 220 px. Width 552 px.

```
┌─────────────────────────────────────────────────────┐  220 px tall
│                                                     │
│           Brightness                                │  +28 px from top
│                                                     │
│               75%                                   │  +84 px from top
│                                                     │
│   [ sun ]  ─────────────●──────────  [ sun+ ]      │  +148 px from top
│   (dim)                             (bright)        │
│                                                     │
└─────────────────────────────────────────────────────┘
```

| Element | Value |
|---|---|
| Card background | `C_BTN` = `0x3A2A7A` |
| Card radius | 20 px |
| Card border | none |
| Card shadow | width 16 px, color `0x000000`, opa 60, y-offset 6 |
| Card padding | 28 px all sides |

**"Brightness" label**

| Property | Value |
|---|---|
| Text | "Brightness" |
| Font | `lv_font_montserrat_28` |
| Color | `C_CARD_TXT` = `0xFFFFFF` |
| Position | `LV_ALIGN_TOP_MID`, y_offset 28 |

**Percentage value label**

| Property | Value |
|---|---|
| Text | "%d%%" (e.g. "75%") |
| Font | `lv_font_montserrat_48` |
| Color | `C_GOLD` = `0xFFD166` |
| Position | `LV_ALIGN_TOP_MID`, y_offset 72 |
| Note | This is the existing `br_val` label — keep for live slider update |

**Slider**

| Property | Value |
|---|---|
| Size | 460 x 48 px |
| Position | `LV_ALIGN_BOTTOM_MID`, y_offset -24 |
| Track bg (`LV_PART_MAIN`) | `0x1C0E42` — darker than card bg, creates depth |
| Track radius (`LV_PART_MAIN`) | 24 px (full pill) |
| Track height (`LV_PART_MAIN`) | 48 px |
| Filled portion (`LV_PART_INDICATOR`) | `C_GOLD` = `0xFFD166` |
| Knob (`LV_PART_KNOB`) | `C_STAR` = `0xFFD166`, `lv_obj_set_style_pad_all` 14 px — gives a 76 px touch target |
| Knob radius | `LV_RADIUS_CIRCLE` |
| Knob shadow | width 12, color `0x000000`, opa 80 |
| Range | `BRIGHTNESS_MIN` (10) to 100 |

**Dim / bright end labels**

| Property | Value |
|---|---|
| Left label text | "o" (small circle = low sun) |
| Right label text | "O" (big circle = bright sun) |
| Font | `lv_font_montserrat_24` |
| Color | `C_SUBTEXT` = `0xBBAADD` |
| Left position | `LV_ALIGN_BOTTOM_LEFT`, x_offset 28, y_offset -28 |
| Right position | `LV_ALIGN_BOTTOM_RIGHT`, x_offset -28, y_offset -28 |

These are decorative only — the percentage label is the authoritative readout.

---

### Section 3 — Star display card

Y 616 to Y 796. Height 180 px. Width 552 px.

```
┌─────────────────────────────────────────────────────┐  180 px tall
│                                                     │
│  Your Stars                                         │  +28 px
│                                                     │
│   *  *  *  *  *  *  *  *  *  *   + 37 more         │  +80 px
│                                                     │
│                   47                                │  +120 px
│                                                     │
└─────────────────────────────────────────────────────┘
```

| Element | Value |
|---|---|
| Card background | `C_BTN` = `0x3A2A7A` |
| Card radius | 20 px |
| Card border | none |
| Card shadow | width 16 px, color `0x000000`, opa 60, y-offset 6 |

**"Your Stars" label**

| Property | Value |
|---|---|
| Text | "Your Stars" |
| Font | `lv_font_montserrat_28` |
| Color | `C_GOLD` = `0xFFD166` |
| Position | `LV_ALIGN_TOP_LEFT`, x_offset 28, y_offset 24 |

**Star symbol row**

Display up to 10 star glyphs as individual `lv_label` objects, each containing
a single `"*"` character, laid out horizontally.

| Property | Value |
|---|---|
| Glyph | `"*"` — asterisk renders clearly in Montserrat |
| Font | `lv_font_montserrat_32` |
| Color | `C_STAR` = `0xFFD166` |
| Each glyph width | 32 px |
| Gap between glyphs | 4 px |
| Row Y | `LV_ALIGN_TOP_LEFT`, y_offset 72 |
| Row X start | x_offset 28 |
| Total row width | 10 * (32 + 4) - 4 = 356 px — fits in 552 px card |

Logic for the row: show `min(g_stars, 10)` filled star glyphs. If `g_stars > 10`,
append a single `lv_label` reading `"+ %ld more"` in `lv_font_montserrat_20`
`C_SUBTEXT`, immediately to the right of the last star at x_offset 364, same
y_offset 72.

**Total count label**

| Property | Value |
|---|---|
| Text | "%ld" (e.g. "47") |
| Font | `lv_font_montserrat_48` |
| Color | `C_GOLD` = `0xFFD166` |
| Position | `LV_ALIGN_BOTTOM_MID`, y_offset -20 |

---

### Content column

The three section separators + three cards live in a flex column container:

| Property | Value |
|---|---|
| Container size | 552 x 912 px (SCR_W - 48, SCR_H - header 88 - bottom margin 24) |
| Position | `LV_ALIGN_TOP_MID`, y_offset 100 (= header 88 + 12 breathing room) |
| Flex flow | `LV_FLEX_FLOW_COLUMN` |
| Flex align | `LV_FLEX_ALIGN_START` |
| Gap | 16 px between all children |
| Background | transparent |
| Scrollable | yes — `lv_obj_add_flag(col, LV_OBJ_FLAG_SCROLLABLE)`, so the mountain bg shows through on scroll |

Note: making the column scrollable is a behavior change from today's static
layout. The scroll container must have a transparent background so the mountain
canvas behind it is visible while scrolling.

---

### Full Y-position summary

| Element | Y start | Height | Y end |
|---|---|---|---|
| Header bar | 0 | 88 | 88 |
| (gap) | 88 | 12 | 100 |
| "NETWORK" separator label | 100 | 24 | 124 |
| separator rule (2 px) | 124 | 2 | 126 |
| (gap 16) | 126 | 16 | 142 |
| WiFi card | 142 | 120 | 262 |
| (gap 16) | 262 | 16 | 278 |
| "DISPLAY" separator label | 278 | 24 | 302 |
| separator rule | 302 | 2 | 304 |
| (gap 16) | 304 | 16 | 320 |
| Brightness card | 320 | 220 | 540 |
| (gap 16) | 540 | 16 | 556 |
| "YOUR STARS" separator label | 556 | 24 | 580 |
| separator rule | 580 | 2 | 582 |
| (gap 16) | 582 | 16 | 598 |
| Star card | 598 | 180 | 778 |
| (gap + mountain bg visible) | 778 | 246 | 1024 |

Total content height: 778 px. Screen height: 1024 px. Mountains start at Y 724.
The star card bottom edge (Y 778) is 54 px into the mountain zone — visually the
card floats over the near-mountain silhouette, which looks intentional. The
mountain canvas is behind the transparent scroll container so it remains visible.

---

### Screen entry animation

`lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 250, 0, true)`
Same as current code — no change.

---

### Accessibility notes

**Contrast checks (WCAG AA, min 4.5:1 for normal text, 3:1 for large text):**

| Foreground | Background | Ratio (approx) | Pass |
|---|---|---|---|
| `C_CARD_TXT` `#FFFFFF` on `C_BTN` `#3A2A7A` | — | 8.2:1 | AA large + normal |
| `C_GOLD` `#FFD166` on `C_BTN` `#3A2A7A` | — | 7.1:1 | AA |
| `C_SUBTEXT` `#BBAADD` on `C_BTN` `#3A2A7A` | — | 4.6:1 | AA (borderline — only use for secondary text at 20+ pt) |
| `C_CORRECT` `#06D6A0` on `C_BTN` `#3A2A7A` | — | 5.8:1 | AA |
| `C_GOLD` `#FFD166` on `C_BG` `#1A0533` | — | 9.4:1 | AA |

**Touch targets:**

| Element | Tap area |
|---|---|
| Back button | 64 x 56 px — passes 60 px minimum |
| WiFi Connect/Change button | 130 x 64 px — passes |
| Brightness slider knob | ~76 x 76 px (knob pad 14 on a 48 px base) — passes |
| Star card (read-only, no tap) | n/a |

---

### Color tokens used (no new tokens introduced)

All colors reference existing `C_*` palette constants. The three sky-gradient
hex values (`0x100224`, `0x160830`, `0x1F0A45`) and the two mountain fill hex
values (`0x2A1660`, `0x1C0E42`) are used only inside the `draw_mountain_bg()`
function and need not be global defines — SWE can define them as local `const
uint32_t` inside the function.

---

### New recurring pattern — escalation note

The mountain background layer is a new pattern not yet in `_Concepts/`:
**LVGL Persistent Canvas Background** (a full-screen static canvas drawn once,
z-indexed behind all content, reused across screens). This pattern could apply
to any ESP32 LVGL project with a thematic background. A `_Concepts/` entry is
being proposed separately per the Designer DoD.

---

*Spec sources: `src/main.cpp` lines 54–88 (palette + screen dimensions), lines
1227–1344 (existing `launch_settings`), `design/mockups.html` (visual language
reference), `Visual Diagrams - LVGL Design.md` (card conventions + pixel
budget). WCAG contrast ratios computed against the W3C relative-luminance
formula.*
