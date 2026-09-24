// clock_faces.cpp — local Orb Imperial + India steampunk faces, side by side on 1024x600.
//
// Port notes (LVGL 8.4 -> 9, two 466x466 faces -> 1024x600 landscape):
//   * Each face draws into its own 466x466 RGB565 canvas exactly as Orb did, so every
//     coordinate in the face code is still dial-local with the centre at (233,233). The
//     canvases are placed on the screen; nothing here knows about the panel size except
//     the two placement constants below.
//   * lv_canvas_draw_* is gone in LVGL 9. Drawing goes through a layer
//     (lv_canvas_init_layer / lv_draw_* / lv_canvas_finish_layer). The polygon hand
//     becomes two triangles.
//   * Both faces use code-drawn hands.  The embossed depth stays baked into each dial,
//     while the small moving geometry avoids full-image transforms and extra PSRAM.
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

#include "art_store.h"                 // dial bitmaps live in the art partition (assets/orb/dial_*.h are the source)

static const char* TAG = "clock_faces";

// ── geometry ──────────────────────────────────────────────────────────────────
#define DIAL_W  466
#define DIAL_H  466
static constexpr float CX = 233.0f;
static constexpr float CY = 233.0f;
static constexpr float DEG2RAD = 3.14159265358979f / 180.0f;

// Two native-size faces fit side by side without resampling: 30 px outer margins,
// 32 px between them, and 67 px above/below.
#define DIAL_LEFT_X   30
#define DIAL_RIGHT_X  (DIAL_LEFT_X + DIAL_W + 32)
#define DIAL_Y        67

// Date window shared by the two instrument faces.
static constexpr int   DATE_WIN_X    = 233;
static constexpr int   DATE_WIN_Y    = 328;

// ── palette (Orb's) ───────────────────────────────────────────────────────────
#define COL_HAND       lv_color_make(0xE6, 0xB8, 0x58)   // steampunk polished brass
#define COL_HAND_EDGE  lv_color_make(0x2B, 0x16, 0x08)   // dark bronze outline
#define COL_LOCAL_HI   lv_color_make(0xF1, 0xF4, 0xF7)   // Imperial raised silver ridge
#define COL_LOCAL_LO   lv_color_make(0x96, 0xA4, 0xB4)
#define COL_LOCAL_EDGE lv_color_make(0x16, 0x23, 0x35)
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
static Face        s_local;
static Face        s_steampunk;
static bool        s_noTime   = false;
static const uint8_t* s_dialLocal = nullptr; // 466x466 RGB565 in mapped flash, or NULL
static const uint8_t* s_dialSteam = nullptr;
static bool        s_artWarned = false;

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

// Two differently lit facets meet along the hand's spine. Which facet receives
// the highlight is chosen from a fixed upper-left light, so the material reads as
// raised metal while the hand rotates instead of carrying its light around with it.
static void draw_hand_faceted(lv_layer_t* L, float pxc, float pyc, float angDeg,
                              float len, float tail, float hw, lv_color_t light,
                              lv_color_t dark, lv_color_t edge) {
    const float a = angDeg * DEG2RAD;
    const float dx = sinf(a), dy = -cosf(a);
    const float qx = cosf(a), qy = sinf(a);
    const float sx = pxc + len * 0.16f * dx, sy = pyc + len * 0.16f * dy;
    const lv_point_precise_t tip = PP(pxc + len * dx, pyc + len * dy);
    const lv_point_precise_t right = PP(sx + hw * qx, sy + hw * qy);
    const lv_point_precise_t back = PP(pxc - tail * dx, pyc - tail * dy);
    const lv_point_precise_t left = PP(sx - hw * qx, sy - hw * qy);

    draw_hand_at(L, pxc, pyc, angDeg, len + 1.5f, tail + 1.5f, hw + 1.4f, edge);
    const float side_dot_light = qx * -0.7071f + qy * -0.7071f;
    tri(L, tip, right, back, side_dot_light > 0 ? light : dark);
    tri(L, tip, back, left, side_dot_light > 0 ? dark : light);
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

// ── time ──────────────────────────────────────────────────────────────────────
// Until NTP has set the clock, show the face at 12:00 with the seconds running and no
// date — the way every oven reads "not set" — rather than a black dial. (Orb's rule.)
static void time_for_faces(struct tm* local, struct tm* india) {
    time_t now = time(NULL);
    localtime_r(&now, local);
    time_t india_now = now + (5 * 60 + 30) * 60;  // IST is fixed UTC+05:30
    gmtime_r(&india_now, india);
    if (now > 1704067200L) { s_noTime = false; return; }
    local->tm_hour = india->tm_hour = 0;
    local->tm_min  = india->tm_min  = 0;
    s_noTime = true;
}

// ── STEAMPUNK INDIA face ──────────────────────────────────────────────────────
static void draw_steampunk(const struct tm* ti) {
    Face& f = s_steampunk;
    if (s_dialSteam) memcpy(f.buf, s_dialSteam, (size_t)DIAL_W * DIAL_H * 2);
    else           memset(f.buf, 0x10, (size_t)DIAL_W * DIAL_H * 2);   // no art: dark plate, hands still tell the time

    lv_layer_t L;
    lv_canvas_init_layer(f.canvas, &L);

    draw_text(&L, 143, 91, 180, &lv_font_montserrat_20, COL_DATE,
              LV_TEXT_ALIGN_CENTER, "INDIA  IST");

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

// ── LOCAL face — Orb Imperial Signal ─────────────────────────────────────────
static void draw_local(const struct tm* ti) {
    Face& f = s_local;
    if (s_dialLocal) memcpy(f.buf, s_dialLocal, (size_t)DIAL_W * DIAL_H * 2);
    else           memset(f.buf, 0x10, (size_t)DIAL_W * DIAL_H * 2);

    lv_layer_t L;
    lv_canvas_init_layer(f.canvas, &L);

    if (!s_noTime) {
        char ds[4];
        snprintf(ds, sizeof(ds), "%d", ti->tm_mday);
        draw_text(&L, DATE_WIN_X - 24, DATE_WIN_Y - 12, 48, &lv_font_montserrat_20,
                  COL_DATE, LV_TEXT_ALIGN_CENTER, ds);
    }

    const float sec  = ti->tm_sec;
    const float mins = ti->tm_min + sec / 60.0f;
    const float hrs  = (ti->tm_hour % 12) + mins / 60.0f;

    const float ha = hrs * 30.0f;
    const float ma = mins * 6.0f;
    // All fixed-light shadows first, then all metal, then seconds and cap.
    draw_hand_at(&L, CX + 3, CY + 5, ha, 116, 20, 8.5f, COL_BLACK);
    draw_hand_at(&L, CX + 3, CY + 5, ma, 190, 26, 7.0f, COL_BLACK);
    draw_hand_faceted(&L, CX, CY, ha, 116, 20, 7.0f,
                      COL_LOCAL_HI, COL_LOCAL_LO, COL_LOCAL_EDGE);
    draw_hand_faceted(&L, CX, CY, ma, 190, 26, 5.5f,
                      COL_LOCAL_HI, COL_LOCAL_LO, COL_LOCAL_EDGE);
    draw_needle_at(&L, CX, CY, sec * 6.0f, 196, 48, 4, COL_LOCAL_EDGE);
    draw_needle_at(&L, CX, CY, sec * 6.0f, 196, 48, 2, COL_RED);
    draw_disc(&L, CX, CY, 9, COL_LOCAL_EDGE);
    draw_disc(&L, CX, CY, 6, COL_LOCAL_HI);
    draw_disc(&L, CX, CY, 3, COL_BLACK);

    lv_canvas_finish_layer(f.canvas, &L);
    lv_obj_invalidate(f.canvas);
}

// ── redraw / tick ─────────────────────────────────────────────────────────────
static void redraw_all(void) {
    if (!s_screen) return;
    struct tm local, india;
    time_for_faces(&local, &india);
    draw_local(&local);
    draw_steampunk(&india);
}

static void tick_cb(lv_timer_t*) {
    if (!s_screen || lv_screen_active() != s_screen) return;
    redraw_all();
}

void clock_faces_refresh(void) { redraw_all(); }

// Screen is being deleted (the next screen loaded with auto-delete): stop the timer and
// forget the object pointers. The two PSRAM canvas buffers are kept for reuse.
static void on_screen_delete(lv_event_t*) {
    if (s_tick) { lv_timer_delete(s_tick); s_tick = nullptr; }
    s_screen = nullptr;
    s_local.canvas = s_steampunk.canvas = nullptr;
}

// ── build ─────────────────────────────────────────────────────────────────────
static bool ensure_buffers(void) {
    const size_t bytes = (size_t)DIAL_W * DIAL_H * 2;
    if (!s_local.buf)  s_local.buf  = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_steampunk.buf) s_steampunk.buf = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_local.buf || !s_steampunk.buf) {
        ESP_LOGE(TAG, "PSRAM alloc for clock canvases failed");
        return false;
    }
    if (!s_dialLocal) { const ArtEntry* e = art_find("dial_local"); if (e && e->w == DIAL_W && e->h == DIAL_H) s_dialLocal = art_data(e); }
    if (!s_dialSteam) { const ArtEntry* e = art_find("dial_steam"); if (e && e->w == DIAL_W && e->h == DIAL_H) s_dialSteam = art_data(e); }
    if ((!s_dialLocal || !s_dialSteam) && !s_artWarned) {
        s_artWarned = true;
        ESP_LOGE(TAG, "dial art missing (local=%d steam=%d) - drawing hands on a plain plate",
                 (int)!!s_dialLocal, (int)!!s_dialSteam);
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

lv_obj_t* clock_faces_build(void) {
    if (!ensure_buffers()) return nullptr;

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, COL_BLACK, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_screen, on_screen_delete, LV_EVENT_DELETE, NULL);

    // Local-time embossed Orb Imperial left, India-time steampunk right.
    make_canvas(s_screen, s_local, DIAL_LEFT_X, DIAL_Y);
    make_canvas(s_screen, s_steampunk, DIAL_RIGHT_X, DIAL_Y);

    redraw_all();
    s_tick = lv_timer_create(tick_cb, 1000, NULL);
    return s_screen;
}
