// rocky.cpp — see rocky.h. Geometry and timings are the pomodoro_esp32_s3 originals,
// multiplied by `scale`; the animation calls are their LVGL 9 spellings.
#include "rocky.h"
#include "app_audio.h"
#include "esp_log.h"
#include <math.h>
#include <stdlib.h>

static const char* TAG = "rocky";

// ── base geometry at scale 1.0 (410-wide original) ────────────────────────────
#define EYE_W     72
#define EYE_H     104
#define EYE_LX   (-60)
#define EYE_RX     60
#define EYE_Y    (-110)
#define MOUTH_D    160
#define MOUTH_Y   (-50)
#define MOUTH_W    12

static lv_obj_t*   s_face    = nullptr;   // container, transparent
static lv_obj_t*   s_eyeL    = nullptr;
static lv_obj_t*   s_eyeR    = nullptr;
static lv_obj_t*   s_mouth   = nullptr;
static lv_timer_t* s_blink   = nullptr;   // random blink, NEUTRAL only
static lv_timer_t* s_hold    = nullptr;   // one-shot: transient expr -> NEUTRAL
static RockyExpr   s_expr    = ROCKY_NEUTRAL;
static float       s_k       = 1.0f;
static bool        s_proudQueued = false;
static uint32_t    s_proudExpires = 0;
static const uint32_t PROUD_TTL_MS = 120000;

static const Note SND_PROUD[] = { {523, 90}, {0, 50}, {784, 140}, {0, 0} };

static inline int S(int v) { return (int)lroundf(v * s_k); }

static bool alive(void) { return s_face && s_eyeL && s_eyeR && s_mouth; }
static bool visible(void) { return alive() && lv_obj_get_screen(s_face) == lv_screen_active(); }

// ── geometry helpers ──────────────────────────────────────────────────────────
static void place_eyes(int dx, int dy) {
    lv_obj_align(s_eyeL, LV_ALIGN_CENTER, S(EYE_LX) + dx, S(EYE_Y) + dy);
    lv_obj_align(s_eyeR, LV_ALIGN_CENTER, S(EYE_RX) + dx, S(EYE_Y) + dy);
}

static void apply_neutral(void) {
    lv_obj_set_size(s_eyeL, S(EYE_W), S(EYE_H));
    lv_obj_set_size(s_eyeR, S(EYE_W), S(EYE_H));
    lv_obj_set_style_radius(s_eyeL, S(EYE_W) / 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_eyeR, S(EYE_W) / 2, LV_PART_MAIN);
    place_eyes(0, 0);
    lv_obj_set_size(s_mouth, S(MOUTH_D), S(MOUTH_D));
    lv_obj_align(s_mouth, LV_ALIGN_CENTER, 0, S(MOUTH_Y));
    lv_arc_set_bg_angles(s_mouth, 45, 135);
    lv_arc_set_angles(s_mouth, 45, 135);
}

static void cancel_anims(void) {
    if (s_hold) { lv_timer_delete(s_hold); s_hold = nullptr; }
    if (s_eyeL)  lv_anim_delete(s_eyeL, NULL);
    if (s_eyeR)  lv_anim_delete(s_eyeR, NULL);
    if (s_mouth) lv_anim_delete(s_mouth, NULL);
}

// ── animations ────────────────────────────────────────────────────────────────
static void anim_eye_h(void* var, int32_t h) {
    lv_obj_t* eye = (lv_obj_t*)var;
    lv_obj_set_height(eye, h);
    lv_obj_align(eye, LV_ALIGN_CENTER, (eye == s_eyeL) ? S(EYE_LX) : S(EYE_RX), S(EYE_Y));
}

static void blink_anim(int lowH, uint32_t ms) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, anim_eye_h);
    lv_anim_set_values(&a, S(EYE_H), S(lowH));
    lv_anim_set_duration(&a, ms);
    lv_anim_set_reverse_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_var(&a, s_eyeL); lv_anim_start(&a);
    lv_anim_set_var(&a, s_eyeR); lv_anim_start(&a);
}

// PROUD: whole face hops up, three sine hops.
static void anim_proud_bounce(void* var, int32_t v) {
    (void)var;
    float s = sinf((float)v * (float)M_PI / 180.0f);
    int dy = (int)lroundf(-10.0f * s_k * fabsf(s));
    place_eyes(0, dy);
    lv_obj_align(s_mouth, LV_ALIGN_CENTER, 0, S(MOUTH_Y) + dy);
}

static void blink_timer_cb(lv_timer_t*) {
    if (!alive() || s_expr != ROCKY_NEUTRAL) return;
    if (!visible()) return;
    if (rand() % 100 < 35) blink_anim(15, 120);
}

static void hold_cb(lv_timer_t*) {
    s_hold = nullptr;   // one-shot, LVGL deletes it
    if (!alive()) return;
    lv_anim_delete(s_eyeL, NULL);
    lv_anim_delete(s_eyeR, NULL);
    lv_anim_delete(s_mouth, NULL);
    s_expr = ROCKY_NEUTRAL;
    apply_neutral();
    if (s_blink) { lv_timer_reset(s_blink); lv_timer_resume(s_blink); }
}

static void hold(uint32_t ms) {
    if (s_hold) lv_timer_delete(s_hold);
    s_hold = lv_timer_create(hold_cb, ms, NULL);
    lv_timer_set_repeat_count(s_hold, 1);
}

// ── lifecycle ─────────────────────────────────────────────────────────────────
static void on_face_delete(lv_event_t*) {
    cancel_anims();
    if (s_blink) { lv_timer_delete(s_blink); s_blink = nullptr; }
    s_face = s_eyeL = s_eyeR = s_mouth = nullptr;
    s_expr = ROCKY_NEUTRAL;
}

static lv_obj_t* make_eye(lv_obj_t* parent) {
    lv_obj_t* e = lv_obj_create(parent);
    lv_obj_set_style_border_width(e, 0, 0);
    lv_obj_set_style_pad_all(e, 0, 0);
    lv_obj_set_style_shadow_width(e, 0, 0);
    lv_obj_set_style_bg_color(e, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(e, LV_OPA_COVER, 0);
    lv_obj_remove_flag(e, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(e, LV_OBJ_FLAG_CLICKABLE);
    return e;
}

lv_obj_t* rocky_create(lv_obj_t* parent, int cx, int cy, float scale) {
    if (s_face) {
        // A previous face still exists on another screen; drop our claim on it. Its own
        // DELETE event will not find us any more, so detach its timers now.
        cancel_anims();
        if (s_blink) { lv_timer_delete(s_blink); s_blink = nullptr; }
        lv_obj_remove_event_cb(s_face, on_face_delete);
    }
    s_k = scale;
    const int w = S(320), h = S(300);

    s_face = lv_obj_create(parent);
    lv_obj_set_size(s_face, w, h);
    lv_obj_set_pos(s_face, cx - w / 2, cy - h / 2);
    lv_obj_set_style_bg_opa(s_face, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_face, 0, 0);
    lv_obj_set_style_pad_all(s_face, 0, 0);
    lv_obj_remove_flag(s_face, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_face, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_face, on_face_delete, LV_EVENT_DELETE, NULL);

    s_eyeL = make_eye(s_face);
    s_eyeR = make_eye(s_face);

    s_mouth = lv_arc_create(s_face);
    lv_obj_remove_style(s_mouth, NULL, LV_PART_KNOB);
    lv_obj_remove_style(s_mouth, NULL, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_mouth, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_mouth, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_mouth, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_mouth, S(MOUTH_W), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(s_mouth, true, LV_PART_MAIN);
    lv_obj_remove_flag(s_mouth, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_mouth, LV_OBJ_FLAG_CLICKABLE);

    // Eyes and mouth sit at the container's centre-ish: the original geometry put the
    // eyes at y-110 and the mouth at y-50 relative to the *screen* centre; here the
    // container centre plays that role, shifted so the face is centred in its own box.
    s_expr = ROCKY_NEUTRAL;
    apply_neutral();
    s_blink = lv_timer_create(blink_timer_cb, 2500, NULL);
    return s_face;
}

// ── expressions ───────────────────────────────────────────────────────────────
void rocky_queue_proud(void) {
    s_proudQueued  = true;
    s_proudExpires = lv_tick_get() + PROUD_TTL_MS;
}

void rocky_set_expression(RockyExpr e) {
    if (!alive()) { if (e == ROCKY_PROUD) rocky_queue_proud(); return; }
    if (!visible()) { if (e == ROCKY_PROUD) rocky_queue_proud(); return; }
    if (e == s_expr) return;
    if (e == ROCKY_NEUTRAL && s_hold) return;   // a transient owns its own return

    static const char* names[] = { "NEUTRAL", "BLINK", "LOOK", "PROUD" };
    ESP_LOGI(TAG, "%s -> %s", names[s_expr], names[e]);

    cancel_anims();
    s_expr = e;
    apply_neutral();

    if (e == ROCKY_NEUTRAL) {
        if (s_blink) { lv_timer_reset(s_blink); lv_timer_resume(s_blink); }
        return;
    }
    if (s_blink) lv_timer_pause(s_blink);

    switch (e) {
        case ROCKY_BLINK:
            blink_anim(15, 120);
            hold(300);
            break;
        case ROCKY_LOOK:
            blink_anim(15, 200);   // one deliberate blink, calm, no sound
            hold(1000);
            break;
        case ROCKY_PROUD: {
            lv_obj_set_height(s_eyeL, S(28));
            lv_obj_set_height(s_eyeR, S(28));
            lv_obj_set_style_radius(s_eyeL, S(14), LV_PART_MAIN);
            lv_obj_set_style_radius(s_eyeR, S(14), LV_PART_MAIN);
            place_eyes(0, 0);
            lv_arc_set_bg_angles(s_mouth, 30, 150);
            lv_arc_set_angles(s_mouth, 30, 150);
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_exec_cb(&a, anim_proud_bounce);
            lv_anim_set_values(&a, 0, 1080);   // 3 hops
            lv_anim_set_duration(&a, 1500);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
            lv_anim_set_var(&a, s_mouth);
            lv_anim_start(&a);
            audio_play(SND_PROUD);
            hold(3200);
            break;
        }
        default: break;
    }
}

void rocky_poll(void) {
    if (!s_proudQueued) return;
    if ((int32_t)(lv_tick_get() - s_proudExpires) >= 0) { s_proudQueued = false; return; }
    if (visible() && s_expr == ROCKY_NEUTRAL && !s_hold) {
        s_proudQueued = false;
        rocky_set_expression(ROCKY_PROUD);
    }
}
