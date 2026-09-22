// clock_faces.cpp — Aviator + Imperial faces from Orb OS, stacked on a 600x1024 screen.
//
// Port notes (LVGL 8.4 -> 9, 466x466 round -> 600x1024 portrait):
//   * Each face draws into its own 466x466 RGB565 canvas exactly as Orb did, so every
//     coordinate in the face code is still dial-local with the centre at (233,233). The
//     canvases are placed on the screen; nothing here knows about the panel size except
//     the two placement constants below.
//   * lv_canvas_draw_* is gone in LVGL 9. Drawing goes through a layer
//     (lv_canvas_init_layer / lv_draw_* / lv_canvas_finish_layer). The polygon hand
//     becomes two triangles.
//   * Orb's hand sprites are LVGL 8 TRUE_COLOR_ALPHA: 3 bytes per pixel, RGB565 lo/hi
//     then alpha, interleaved. LVGL 9's RGB565A8 wants two planes: the whole RGB565
//     map, then the whole A8 map (alpha stride = half the colour stride). They are
//     split once at first build into PSRAM; the flash arrays stay byte-for-byte the
//     files Orb ships.
//   * The Aviator hands are lv_image objects rotated with lv_image_set_rotation (still
//     0.1 degree units) around lv_image_set_pivot, sitting above the canvas. Orb recorded
//     that this pairing painted opaque bounding boxes on LVGL 8.4; LVGL 9's draw unit
//     handles RGB565A8 transforms through an ARGB8888 temp buffer, so it is tried here
//     first and the Office-style hand-rolled blend is the fallback if it misbehaves.
//
// Attribution: dial and hand art, face geometry and the drawing routines are from
// Ziplock78/orb-os, MIT, Copyright (c) 2026 Quique Tortosa and Zion Brock.
// See src/orb/LICENSE.orb-os.

#include "clock_faces.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "orb/dial_img.h"              // DIAL_IMG  — Imperial Signal (blue)
#include "orb/dial_avi.h"              // DIAL_AVI  — Aviator (cream), AVI_SUB_X/Y sub-dial centre
#include "orb/hand_hour_img.h"         // HAND_HOUR_IMG_MAP + pivot
#include "orb/hand_min_img.h"          // HAND_MIN_IMG_MAP + pivot
#include "orb/hand_hour_shadow_img.h"  // pre-blurred silhouettes, same pivots offset by the light
#include "orb/hand_min_shadow_img.h"

static const char* TAG = "clock_faces";

// ── geometry ──────────────────────────────────────────────────────────────────
#define DIAL_W  466
#define DIAL_H  466
static constexpr float CX = 233.0f;
static constexpr float CY = 233.0f;
static constexpr float DEG2RAD = 3.14159265358979f / 180.0f;

// Placement on the 600x1024 portrait screen: two dials stacked, 30 px top and bottom
// margin, 32 px between. Centred horizontally.
#define SCREEN_W_PX   600
#define SCREEN_H_PX   1024
#define DIAL_X        ((SCREEN_W_PX - DIAL_W) / 2)     // 67
#define DIAL_TOP_Y    30
#define DIAL_BOT_Y    (SCREEN_H_PX - 30 - DIAL_H)     // 528

// Imperial date window and Aviator banner arc — from Orb's clock_view.cpp.
static constexpr int   DATE_WIN_X    = 233;
static constexpr int   DATE_WIN_Y    = 328;
static constexpr float AVI_DATE_R    = 184.0f;
static constexpr float AVI_DATE_MID  = 180.0f;
static constexpr float AVI_DATE_STEP = 4.0f;

// Fixed light: shadows fall toward 7 o'clock in screen space, not rotated with the hand.
static constexpr float HAND_SHADOW_DX = -4.0f;
static constexpr float HAND_SHADOW_DY =  7.0f;

// ── palette (Orb's) ───────────────────────────────────────────────────────────
#define COL_HAND       lv_color_make(0xE4, 0xE9, 0xF0)   // imperial silver
#define COL_HAND_EDGE  lv_color_make(0x0A, 0x16, 0x28)   // imperial hand outline
#define COL_DATE       lv_color_make(0xF2, 0xF5, 0xF9)
#define COL_LUME_EDGE  lv_color_make(0x38, 0x2E, 0x18)   // dark sepia
#define COL_GOLD       lv_color_make(0xCB, 0xA5, 0x54)   // polished gold boss
#define COL_RED        lv_color_make(0xB2, 0x3A, 0x2C)   // red seconds hand
#define COL_DATE_DARK  lv_color_make(0x2A, 0x24, 0x18)   // date text on cream
#define COL_BLACK      lv_color_make(0x00, 0x00, 0x00)

// ── state ─────────────────────────────────────────────────────────────────────
struct Face {
    lv_obj_t*  canvas = nullptr;
    uint16_t*  buf    = nullptr;   // 466x466 RGB565, PSRAM, persistent
};

static lv_obj_t*   s_screen   = nullptr;
static lv_timer_t* s_tick     = nullptr;
static Face        s_aviator;
static Face        s_imperial;
static bool        s_noTime   = false;

// Aviator hand sprites (lv_image objects on the screen, above the canvas).
static lv_obj_t* s_hourImg    = nullptr;
static lv_obj_t* s_minImg     = nullptr;
static lv_obj_t* s_hourShadow = nullptr;
static lv_obj_t* s_minShadow  = nullptr;

// Converted RGB565A8 sprite descriptors (data in PSRAM, persistent).
static lv_image_dsc_t s_dscHour, s_dscMin, s_dscHourShadow, s_dscMinShadow;

// ── sprite conversion: interleaved (lo,hi,a) -> planar RGB565 + A8 ───────────
static bool convert_sprite(lv_image_dsc_t* out, const uint8_t* src, int w, int h) {
    const size_t npx = (size_t)w * h;
    uint8_t* dst = (uint8_t*)heap_caps_malloc(npx * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!dst) return false;
    uint16_t* rgb = (uint16_t*)dst;
    uint8_t*  a   = dst + npx * 2;
    for (size_t i = 0; i < npx; ++i) {
        rgb[i] = (uint16_t)(src[i * 3] | (src[i * 3 + 1] << 8));
        a[i]   = src[i * 3 + 2];
    }
    memset(out, 0, sizeof(*out));
    out->header.magic  = LV_IMAGE_HEADER_MAGIC;
    out->header.cf     = LV_COLOR_FORMAT_RGB565A8;
    out->header.w      = w;
    out->header.h      = h;
    out->header.stride = w * 2;
    out->data_size     = npx * 3;
    out->data          = dst;
    return true;
}

// ── canvas drawing helpers (LVGL 9 layer API) ────────────────────────────────
static inline lv_point_precise_t PP(float x, float y) {
    lv_point_precise_t p;
    p.x = (lv_value_precise_t)lroundf(x);
    p.y = (lv_value_precise_t)lroundf(y);
    return p;
}

static void tri(lv_layer_t* L, lv_point_precise_t a, lv_point_precise_t b, lv_point_precise_t c, lv_color_t col) {
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.p[0] = a; d.p[1] = b; d.p[2] = c;
    d.color = col;
    d.opa   = LV_OPA_COVER;
    lv_draw_triangle(L, &d);
}

// Tapered "dauphine" hand pivoting at (pxc, pyc). Orb drew one 4-point polygon; LVGL 9
// has no polygon primitive so it is two triangles sharing the tip-tail diagonal.
static void draw_hand_at(lv_layer_t* L, float pxc, float pyc, float angDeg, float len, float tail,
                         float hw, lv_color_t col) {
    const float a  = angDeg * DEG2RAD;
    const float dx = sinf(a),  dy = -cosf(a);
    const float qx = cosf(a),  qy =  sinf(a);
    const float sx = pxc + len * 0.16f * dx, sy = pyc + len * 0.16f * dy;
    const lv_point_precise_t tip   = PP(pxc + len * dx,  pyc + len * dy);
    const lv_point_precise_t right = PP(sx + hw * qx,    sy + hw * qy);
    const lv_point_precise_t back  = PP(pxc - tail * dx, pyc - tail * dy);
    const lv_point_precise_t left  = PP(sx - hw * qx,    sy - hw * qy);
    tri(L, tip, right, back, col);
    tri(L, tip, back, left, col);
}

static void draw_hand_edged(lv_layer_t* L, float pxc, float pyc, float angDeg, float len, float tail,
                            float hw, lv_color_t fill, lv_color_t edge) {
    draw_hand_at(L, pxc, pyc, angDeg, len + 1.5f, tail + 1.5f, hw + 1.4f, edge);
    draw_hand_at(L, pxc, pyc, angDeg, len,        tail,        hw,        fill);
}

static void draw_disc(lv_layer_t* L, float ccx, float ccy, float r, lv_color_t col) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = col;
    d.bg_opa   = LV_OPA_COVER;
    d.radius   = LV_RADIUS_CIRCLE;
    lv_area_t a;
    a.x1 = (int32_t)lroundf(ccx - r);
    a.y1 = (int32_t)lroundf(ccy - r);
    a.x2 = a.x1 + (int32_t)lroundf(2 * r) - 1;
    a.y2 = a.y1 + (int32_t)lroundf(2 * r) - 1;
    lv_draw_rect(L, &d, &a);
}

// Thin needle (line) pivoting at (pxc, pyc) — the seconds hands.
static void draw_needle_at(lv_layer_t* L, float pxc, float pyc, float angDeg, float len, float tail,
                           float width, lv_color_t col) {
    const float a  = angDeg * DEG2RAD;
    const float dx = sinf(a), dy = -cosf(a);
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.p1    = PP(pxc - tail * dx, pyc - tail * dy);
    ld.p2    = PP(pxc + len * dx,  pyc + len * dy);
    ld.color = col;
    ld.opa   = LV_OPA_COVER;
    ld.width = (int32_t)lroundf(width);
    ld.round_start = 1;
    ld.round_end   = 1;
    lv_draw_line(L, &ld);
}

static void draw_text(lv_layer_t* L, int x, int y, int w, const lv_font_t* font, lv_color_t col,
                      lv_text_align_t align, const char* txt) {
    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.text  = txt;
    ld.font  = font;
    ld.color = col;
    ld.align = align;
    lv_area_t a;
    a.x1 = x; a.y1 = y;
    a.x2 = x + w - 1;
    a.y2 = y + lv_font_get_line_height(font);
    lv_draw_label(L, &ld, &a);
}

// Text curved along an arc centred at (cx,cy), radius R, centred on midDeg (clock angle:
// 0 = 12 o'clock, 180 = 6). Characters stay upright along the curve.
static void draw_arc_text(lv_layer_t* L, float cx, float cy, float R, float midDeg, float stepDeg,
                          const char* txt, const lv_font_t* font, lv_color_t col) {
    const int n = (int)strlen(txt);
    const float halfH = lv_font_get_line_height(font) * 0.5f;
    for (int i = 0; i < n; ++i) {
        const float a = (midDeg + ((n - 1) * 0.5f - i) * stepDeg) * DEG2RAD;
        const float x = cx + R * sinf(a);
        const float y = cy - R * cosf(a);
        char c[2] = { txt[i], 0 };
        draw_text(L, (int)lroundf(x - 12), (int)lroundf(y - halfH), 24, font, col, LV_TEXT_ALIGN_CENTER, c);
    }
}

// ── time ──────────────────────────────────────────────────────────────────────
// Until NTP has set the clock, show the face at 12:00 with the seconds running and no
// date — the way every oven reads "not set" — rather than a black dial. (Orb's rule.)
static void time_for_face(struct tm* ti) {
    time_t now = time(NULL);
    localtime_r(&now, ti);
    if (now > 1704067200L) { s_noTime = false; return; }
    ti->tm_hour = 0; ti->tm_min = 0;
    s_noTime = true;
}

// ── IMPERIAL face ─────────────────────────────────────────────────────────────
static void draw_imperial(const struct tm* ti) {
    Face& f = s_imperial;
    memcpy(f.buf, DIAL_IMG, sizeof(DIAL_IMG));

    lv_layer_t L;
    lv_canvas_init_layer(f.canvas, &L);

    if (!s_noTime) {
        char ds[4];
        snprintf(ds, sizeof(ds), "%d", ti->tm_mday);
        draw_text(&L, DATE_WIN_X - 24, DATE_WIN_Y - 12, 48, &lv_font_montserrat_20, COL_DATE,
                  LV_TEXT_ALIGN_CENTER, ds);
    }

    const float sec  = ti->tm_sec;
    const float mins = ti->tm_min + sec / 60.0f;
    const float hrs  = (ti->tm_hour % 12) + mins / 60.0f;

    draw_hand_edged(&L, CX, CY, hrs  * 30.0f, 116, 20, 7.0f, COL_HAND, COL_HAND_EDGE);
    draw_hand_edged(&L, CX, CY, mins * 6.0f,  190, 26, 5.5f, COL_HAND, COL_HAND_EDGE);

    draw_needle_at(&L, CX, CY, sec * 6.0f, 196, 48, 4, COL_HAND_EDGE);
    draw_needle_at(&L, CX, CY, sec * 6.0f, 196, 48, 2, COL_HAND);
    draw_disc(&L, CX, CY, 8, COL_HAND);
    draw_disc(&L, CX, CY, 3, COL_BLACK);

    lv_canvas_finish_layer(f.canvas, &L);
    lv_obj_invalidate(f.canvas);
}

// ── AVIATOR face ──────────────────────────────────────────────────────────────
static void draw_aviator(const struct tm* ti) {
    Face& f = s_aviator;
    memcpy(f.buf, DIAL_AVI, sizeof(DIAL_AVI));

    lv_layer_t L;
    lv_canvas_init_layer(f.canvas, &L);

    // date curved along the banner at the bottom ("Mon 27th")
    if (!s_noTime) {
        int day = ti->tm_mday;
        const char* suf = "th";
        if (day < 11 || day > 13) {
            switch (day % 10) { case 1: suf = "st"; break; case 2: suf = "nd"; break; case 3: suf = "rd"; break; }
        }
        char wd[8]; strftime(wd, sizeof(wd), "%a", ti);
        char ds[16]; snprintf(ds, sizeof(ds), "%s %d%s", wd, day, suf);
        draw_arc_text(&L, CX, CY, AVI_DATE_R, AVI_DATE_MID, AVI_DATE_STEP, ds, &lv_font_montserrat_18, COL_DATE_DARK);
    }

    const float sec  = ti->tm_sec;
    const float mins = ti->tm_min + sec / 60.0f;
    const float hrs  = (ti->tm_hour % 12) + mins / 60.0f;

    // red small seconds in the sub-dial — on the canvas, so under the hand sprites
    draw_needle_at(&L, AVI_SUB_X, AVI_SUB_Y, sec * 6.0f, 44, 10, 2, COL_RED);
    draw_disc(&L, AVI_SUB_X, AVI_SUB_Y, 3, COL_RED);

    // centre boss under the hand sprites — shows through the ring holes as the pin
    draw_disc(&L, CX, CY, 9, COL_LUME_EDGE);
    draw_disc(&L, CX, CY, 5, COL_GOLD);

    lv_canvas_finish_layer(f.canvas, &L);
    lv_obj_invalidate(f.canvas);

    // hour + minute hand sprites, each rotated about its own pivot ring; the shadows get
    // the same angle and are offset by the fixed light vector (see build).
    if (s_hourImg && s_minImg) {
        const int32_t hourAngle = (int32_t)lroundf(hrs  * 300.0f);   // 30 deg/hr * 10
        const int32_t minAngle  = (int32_t)lroundf(mins * 60.0f);    // 6 deg/min * 10
        lv_image_set_rotation(s_hourImg, hourAngle);
        lv_image_set_rotation(s_minImg,  minAngle);
        if (s_hourShadow && s_minShadow) {
            lv_image_set_rotation(s_hourShadow, hourAngle);
            lv_image_set_rotation(s_minShadow,  minAngle);
        }
    }
}

// ── redraw / tick ─────────────────────────────────────────────────────────────
static void redraw_all(void) {
    if (!s_screen) return;
    struct tm ti;
    time_for_face(&ti);
    draw_aviator(&ti);
    draw_imperial(&ti);
}

static void tick_cb(lv_timer_t*) {
    if (!s_screen || lv_screen_active() != s_screen) return;
    redraw_all();
}

void clock_faces_refresh(void) { redraw_all(); }

// Screen is being deleted (the next screen loaded with auto-delete): stop the timer and
// forget the object pointers. Buffers and converted sprites are kept.
static void on_screen_delete(lv_event_t*) {
    if (s_tick) { lv_timer_delete(s_tick); s_tick = nullptr; }
    s_screen = nullptr;
    s_aviator.canvas = s_imperial.canvas = nullptr;
    s_hourImg = s_minImg = s_hourShadow = s_minShadow = nullptr;
}

// ── build ─────────────────────────────────────────────────────────────────────
static bool ensure_buffers(void) {
    const size_t bytes = (size_t)DIAL_W * DIAL_H * 2;
    if (!s_aviator.buf)  s_aviator.buf  = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_imperial.buf) s_imperial.buf = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_aviator.buf || !s_imperial.buf) {
        ESP_LOGE(TAG, "PSRAM alloc for clock canvases failed");
        return false;
    }
    static bool sprites_done = false;
    if (!sprites_done) {
        bool ok = true;
        ok &= convert_sprite(&s_dscHour,       HAND_HOUR_IMG_MAP,        HAND_HOUR_IMG_W,        HAND_HOUR_IMG_H);
        ok &= convert_sprite(&s_dscMin,        HAND_MIN_IMG_MAP,         HAND_MIN_IMG_W,         HAND_MIN_IMG_H);
        ok &= convert_sprite(&s_dscHourShadow, HAND_HOUR_SHADOW_IMG_MAP, HAND_HOUR_SHADOW_IMG_W, HAND_HOUR_SHADOW_IMG_H);
        ok &= convert_sprite(&s_dscMinShadow,  HAND_MIN_SHADOW_IMG_MAP,  HAND_MIN_SHADOW_IMG_W,  HAND_MIN_SHADOW_IMG_H);
        if (!ok) { ESP_LOGE(TAG, "hand sprite conversion failed"); return false; }
        sprites_done = true;
        ESP_LOGI(TAG, "hand sprites converted to RGB565A8 (%u bytes)",
                 (unsigned)(s_dscHour.data_size + s_dscMin.data_size + s_dscHourShadow.data_size + s_dscMinShadow.data_size));
    }
    return true;
}

static lv_obj_t* make_canvas(lv_obj_t* parent, Face& f, int x, int y) {
    f.canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(f.canvas, f.buf, DIAL_W, DIAL_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(f.canvas, x, y);
    lv_obj_remove_flag(f.canvas, LV_OBJ_FLAG_CLICKABLE);
    return f.canvas;
}

// A hand sprite placed so its pivot sits at the dial centre (+ optional light offset).
static lv_obj_t* make_hand(lv_obj_t* parent, const lv_image_dsc_t* dsc, int pivotX, int pivotY,
                           int dialX, int dialY, float offX, float offY) {
    lv_obj_t* img = lv_image_create(parent);
    lv_image_set_src(img, dsc);
    lv_image_set_pivot(img, pivotX, pivotY);
    lv_image_set_antialias(img, true);
    lv_obj_set_pos(img, dialX + (int)lroundf(CX + offX) - pivotX,
                        dialY + (int)lroundf(CY + offY) - pivotY);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    return img;
}

lv_obj_t* clock_faces_build(void) {
    if (!ensure_buffers()) return nullptr;

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, COL_BLACK, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_screen, on_screen_delete, LV_EVENT_DELETE, NULL);

    // Aviator on top, Imperial below.
    make_canvas(s_screen, s_aviator,  DIAL_X, DIAL_TOP_Y);
    make_canvas(s_screen, s_imperial, DIAL_X, DIAL_BOT_Y);

    // Aviator hands: shadows first (so they render underneath), then the hands.
    s_hourShadow = make_hand(s_screen, &s_dscHourShadow, HAND_HOUR_SHADOW_IMG_PIVOT_X, HAND_HOUR_SHADOW_IMG_PIVOT_Y,
                             DIAL_X, DIAL_TOP_Y, HAND_SHADOW_DX, HAND_SHADOW_DY);
    s_minShadow  = make_hand(s_screen, &s_dscMinShadow,  HAND_MIN_SHADOW_IMG_PIVOT_X,  HAND_MIN_SHADOW_IMG_PIVOT_Y,
                             DIAL_X, DIAL_TOP_Y, HAND_SHADOW_DX, HAND_SHADOW_DY);
    s_hourImg    = make_hand(s_screen, &s_dscHour, HAND_HOUR_IMG_PIVOT_X, HAND_HOUR_IMG_PIVOT_Y,
                             DIAL_X, DIAL_TOP_Y, 0, 0);
    s_minImg     = make_hand(s_screen, &s_dscMin,  HAND_MIN_IMG_PIVOT_X,  HAND_MIN_IMG_PIVOT_Y,
                             DIAL_X, DIAL_TOP_Y, 0, 0);

    redraw_all();
    s_tick = lv_timer_create(tick_cb, 1000, NULL);
    return s_screen;
}
