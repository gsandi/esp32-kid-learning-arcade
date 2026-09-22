// focus_apps.cpp — Rocky Pomodoro + independent stopwatch.
//
// This intentionally ports the useful behavior from pomodoro_esp32_s3, not its
// hardware assumptions: touch controls replace tilt/shake, while Rocky keeps his
// neutral blink, tap reaction and proud completion animation.

#include "focus_apps.h"
#include "app_audio.h"
#include "art_store.h"
#include "rocky.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char* TAG = "focus_apps";
static constexpr uint32_t BG       = 0x120925;
static constexpr uint32_t PANEL    = 0x241440;
static constexpr uint32_t TEXT     = 0xFFF7E8;
static constexpr uint32_t MUTED    = 0xB8A9CB;
static constexpr uint32_t ORANGE   = 0xF47A3A;
static constexpr uint32_t TEAL     = 0x35C7C2;
static constexpr uint32_t GOLD     = 0xF4C95D;
static constexpr uint32_t PURPLE   = 0x7652B8;

static const Note SND_DONE[] = {{523, 90}, {0, 45}, {659, 90}, {0, 45}, {784, 180}, {0, 0}};

enum PomoState {
    POMO_READY,
    POMO_FOCUS_RUNNING,
    POMO_FOCUS_PAUSED,
    POMO_FOCUS_DONE,
    POMO_BREAK_RUNNING,
    POMO_BREAK_PAUSED,
    POMO_BREAK_DONE,
};

static PomoState s_pomo = POMO_READY;
static uint32_t  s_pomo_remaining = 25 * 60;
static uint32_t  s_pomo_total = 25 * 60;
static int64_t   s_pomo_deadline_us = 0;
static uint16_t  s_focus_minutes = 25;

static bool      s_sw_running = false;
static int64_t   s_sw_started_us = 0;
static int64_t   s_sw_accum_us = 0;
static uint32_t  s_laps[3] = {};
static uint8_t   s_lap_count = 0;

static lv_timer_t* s_service = nullptr;
static lv_image_dsc_t s_bg_rocky = {};
static lv_image_dsc_t s_bg_stopwatch = {};

// Pomodoro screen refs. They are nulled by the screen DELETE event.
static lv_obj_t* s_pomo_scr = nullptr;
static lv_obj_t* s_pomo_digits = nullptr;
static lv_obj_t* s_pomo_status = nullptr;
static lv_obj_t* s_pomo_hint = nullptr;
static lv_obj_t* s_pomo_progress = nullptr;
static lv_obj_t* s_pomo_presets = nullptr;
static lv_obj_t* s_pomo_controls = nullptr;
static lv_obj_t* s_pomo_primary_lbl = nullptr;

// Stopwatch screen refs.
static lv_obj_t* s_sw_scr = nullptr;
static lv_obj_t* s_sw_digits = nullptr;
static lv_obj_t* s_sw_primary_lbl = nullptr;
static lv_obj_t* s_sw_laps_lbl = nullptr;

static bool pomo_is_running(void) {
    return s_pomo == POMO_FOCUS_RUNNING || s_pomo == POMO_BREAK_RUNNING;
}

static bool pomo_is_paused(void) {
    return s_pomo == POMO_FOCUS_PAUSED || s_pomo == POMO_BREAK_PAUSED;
}

bool focus_pomodoro_running(void) {
    return s_pomo != POMO_READY && s_pomo != POMO_BREAK_DONE;
}

bool focus_stopwatch_running(void) { return s_sw_running; }

static void style_screen(lv_obj_t* scr) {
    lv_obj_set_style_bg_color(scr, lv_color_hex(BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font,
                       uint32_t color) {
    lv_obj_t* obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    return obj;
}

static lv_obj_t* button(lv_obj_t* parent, const char* text, int w, int h,
                        uint32_t color, lv_event_cb_t cb, void* user = nullptr) {
    lv_obj_t* obj = lv_button_create(parent);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(PURPLE), LV_STATE_PRESSED);
    lv_obj_set_style_radius(obj, 22, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, user);
    lv_obj_t* txt = label(obj, text, &lv_font_montserrat_24, TEXT);
    lv_obj_center(txt);
    return obj;
}

static void add_header(lv_obj_t* scr, const char* title, lv_event_cb_t home_cb) {
    lv_obj_t* home = button(scr, "<", 64, 54, PANEL, home_cb);
    lv_obj_align(home, LV_ALIGN_TOP_LEFT, 18, 18);
    lv_obj_t* t = label(scr, title, &lv_font_montserrat_28, GOLD);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 28);
}

static void format_clock(char* out, size_t n, uint32_t sec, bool hours) {
    uint32_t h = sec / 3600;
    uint32_t m = (sec / 60) % 60;
    uint32_t s = sec % 60;
    if (hours || h) snprintf(out, n, "%lu:%02lu:%02lu", (unsigned long)h,
                             (unsigned long)m, (unsigned long)s);
    else snprintf(out, n, "%02lu:%02lu", (unsigned long)m, (unsigned long)s);
}

static void pomo_refresh(void) {
    if (!s_pomo_scr || lv_screen_active() != s_pomo_scr) return;

    char time_buf[16];
    format_clock(time_buf, sizeof(time_buf), s_pomo_remaining, false);
    lv_label_set_text(s_pomo_digits, time_buf);

    const char* status = "READY TO FOCUS";
    const char* hint = "Choose a focus block";
    const char* primary = "Pause";
    switch (s_pomo) {
        case POMO_FOCUS_RUNNING: status = "FOCUS"; hint = "Tap Rocky for a hello"; break;
        case POMO_FOCUS_PAUSED:  status = "FOCUS PAUSED"; hint = "Your time is safe"; primary = "Resume"; break;
        case POMO_FOCUS_DONE:    status = "NICE WORK"; hint = "Rocky is proud of you"; primary = "5 min break"; break;
        case POMO_BREAK_RUNNING: status = "BREAK"; hint = "Breathe, stretch, reset"; break;
        case POMO_BREAK_PAUSED:  status = "BREAK PAUSED"; hint = "Resume when ready"; primary = "Resume"; break;
        case POMO_BREAK_DONE:    status = "BREAK COMPLETE"; hint = "Ready for another round?"; primary = "Finish"; break;
        default: break;
    }
    lv_label_set_text(s_pomo_status, status);
    lv_label_set_text(s_pomo_hint, hint);
    lv_label_set_text(s_pomo_primary_lbl, primary);

    bool ready = s_pomo == POMO_READY;
    if (ready) {
        lv_obj_remove_flag(s_pomo_presets, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_pomo_controls, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_pomo_presets, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_pomo_controls, LV_OBJ_FLAG_HIDDEN);
    }
    int pct = s_pomo_total ? (int)(((uint64_t)(s_pomo_total - s_pomo_remaining) * 100) / s_pomo_total) : 0;
    lv_bar_set_value(s_pomo_progress, pct, LV_ANIM_OFF);
}

static void pomo_complete(void) {
    if (s_pomo == POMO_FOCUS_RUNNING) s_pomo = POMO_FOCUS_DONE;
    else if (s_pomo == POMO_BREAK_RUNNING) s_pomo = POMO_BREAK_DONE;
    else return;
    s_pomo_remaining = 0;
    audio_play(SND_DONE);
    rocky_queue_proud();
    rocky_poll();
    ESP_LOGI(TAG, "%s complete", s_pomo == POMO_FOCUS_DONE ? "focus" : "break");
}

static void pomo_update_model(void) {
    if (!pomo_is_running()) return;
    int64_t left_us = s_pomo_deadline_us - esp_timer_get_time();
    if (left_us <= 0) {
        pomo_complete();
        return;
    }
    s_pomo_remaining = (uint32_t)((left_us + 999999) / 1000000);
}

static uint32_t sw_elapsed_sec(void) {
    int64_t us = s_sw_accum_us;
    if (s_sw_running) us += esp_timer_get_time() - s_sw_started_us;
    return us > 0 ? (uint32_t)(us / 1000000) : 0;
}

static void sw_refresh(void) {
    if (!s_sw_scr || lv_screen_active() != s_sw_scr) return;
    char buf[20];
    format_clock(buf, sizeof(buf), sw_elapsed_sec(), sw_elapsed_sec() >= 3600);
    lv_label_set_text(s_sw_digits, buf);
    lv_label_set_text(s_sw_primary_lbl, s_sw_running ? "Pause" : (sw_elapsed_sec() ? "Resume" : "Start"));
    char laps[96] = "";
    for (uint8_t i = 0; i < s_lap_count; ++i) {
        char lap[40], tm[20];
        format_clock(tm, sizeof(tm), s_laps[i], s_laps[i] >= 3600);
        snprintf(lap, sizeof(lap), "Lap %u   %s%s", (unsigned)(i + 1), tm,
                 i + 1 < s_lap_count ? "\n" : "");
        strncat(laps, lap, sizeof(laps) - strlen(laps) - 1);
    }
    lv_label_set_text(s_sw_laps_lbl, laps[0] ? laps : "No laps yet");
}

static void service_cb(lv_timer_t*) {
    pomo_update_model();
    pomo_refresh();
    sw_refresh();
    rocky_poll();
}

void focus_apps_init(void) {
    art_image("bg_rocky", &s_bg_rocky);
    art_image("bg_stopwatch", &s_bg_stopwatch);
    if (!s_service) s_service = lv_timer_create(service_cb, 250, nullptr);
}

static void pomo_screen_deleted(lv_event_t*) {
    s_pomo_scr = s_pomo_digits = s_pomo_status = s_pomo_hint = nullptr;
    s_pomo_progress = s_pomo_presets = s_pomo_controls = s_pomo_primary_lbl = nullptr;
}

static void on_rocky_tap(lv_event_t*) { rocky_set_expression(ROCKY_LOOK); }

static void on_preset(lv_event_t* e) {
    s_focus_minutes = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    s_pomo_total = s_pomo_remaining = (uint32_t)s_focus_minutes * 60;
    s_pomo_deadline_us = esp_timer_get_time() + (int64_t)s_pomo_remaining * 1000000;
    s_pomo = POMO_FOCUS_RUNNING;
    rocky_set_expression(ROCKY_LOOK);
    pomo_refresh();
}

static void on_pomo_primary(lv_event_t*) {
    if (pomo_is_running()) {
        pomo_update_model();
        s_pomo = (s_pomo == POMO_FOCUS_RUNNING) ? POMO_FOCUS_PAUSED : POMO_BREAK_PAUSED;
    } else if (pomo_is_paused()) {
        s_pomo_deadline_us = esp_timer_get_time() + (int64_t)s_pomo_remaining * 1000000;
        s_pomo = (s_pomo == POMO_FOCUS_PAUSED) ? POMO_FOCUS_RUNNING : POMO_BREAK_RUNNING;
    } else if (s_pomo == POMO_FOCUS_DONE) {
        s_pomo_total = s_pomo_remaining = 5 * 60;
        s_pomo_deadline_us = esp_timer_get_time() + (int64_t)s_pomo_remaining * 1000000;
        s_pomo = POMO_BREAK_RUNNING;
    } else if (s_pomo == POMO_BREAK_DONE) {
        s_pomo_total = s_pomo_remaining = (uint32_t)s_focus_minutes * 60;
        s_pomo = POMO_READY;
    }
    pomo_refresh();
}

static void on_pomo_cancel(lv_event_t*) {
    s_pomo = POMO_READY;
    s_pomo_total = s_pomo_remaining = (uint32_t)s_focus_minutes * 60;
    pomo_refresh();
}

void focus_show_pomodoro(lv_event_cb_t home_cb) {
    ESP_LOGI(TAG, "opening Rocky Focus");
    pomo_update_model();
    lvgl_port_lock(0);
    lv_obj_t* scr = lv_obj_create(nullptr);
    style_screen(scr);
    s_pomo_scr = scr;
    lv_obj_add_event_cb(scr, pomo_screen_deleted, LV_EVENT_DELETE, nullptr);
    add_header(scr, "ROCKY FOCUS", home_cb);

    if (s_bg_rocky.data) {
        lv_obj_t* backdrop = lv_image_create(scr);
        lv_image_set_src(backdrop, &s_bg_rocky);
        lv_obj_set_pos(backdrop, 20, 104);
        lv_obj_set_style_radius(backdrop, 28, 0);
        lv_obj_set_style_clip_corner(backdrop, true, 0);
        lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_CLICKABLE);
    }

    lv_obj_t* face = rocky_create(scr, 300, 318, 0.72f);
    lv_obj_add_flag(face, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(face, on_rocky_tap, LV_EVENT_CLICKED, nullptr);

    s_pomo_status = label(scr, "", &lv_font_montserrat_24, ORANGE);
    lv_obj_align(s_pomo_status, LV_ALIGN_TOP_MID, 0, 455);
    s_pomo_digits = label(scr, "25:00", &lv_font_montserrat_48, TEXT);
    lv_obj_align(s_pomo_digits, LV_ALIGN_TOP_MID, 0, 500);
    s_pomo_hint = label(scr, "", &lv_font_montserrat_20, MUTED);
    lv_obj_align(s_pomo_hint, LV_ALIGN_TOP_MID, 0, 600);

    s_pomo_progress = lv_bar_create(scr);
    lv_obj_set_size(s_pomo_progress, 500, 12);
    lv_obj_align(s_pomo_progress, LV_ALIGN_TOP_MID, 0, 650);
    lv_obj_set_style_bg_color(s_pomo_progress, lv_color_hex(PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_pomo_progress, lv_color_hex(ORANGE), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_pomo_progress, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(s_pomo_progress, 6, LV_PART_INDICATOR);
    lv_bar_set_range(s_pomo_progress, 0, 100);

    s_pomo_presets = lv_obj_create(scr);
    lv_obj_set_size(s_pomo_presets, 560, 210);
    lv_obj_align(s_pomo_presets, LV_ALIGN_BOTTOM_MID, 0, -58);
    lv_obj_set_style_bg_opa(s_pomo_presets, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_pomo_presets, 0, 0);
    lv_obj_set_style_pad_all(s_pomo_presets, 0, 0);
    lv_obj_remove_flag(s_pomo_presets, LV_OBJ_FLAG_SCROLLABLE);
    const uint16_t mins[] = {10, 25, 45};
    const char* names[] = {"10 min", "25 min", "45 min"};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t* b = button(s_pomo_presets, names[i], 168, 96, i == 1 ? ORANGE : PANEL,
                             on_preset, (void*)(uintptr_t)mins[i]);
        lv_obj_set_pos(b, 8 + i * 184, 20);
    }
    lv_obj_t* preset_hint = label(s_pomo_presets, "Short sprint     Classic focus     Deep focus",
                                  &lv_font_montserrat_18, MUTED);
    lv_obj_align(preset_hint, LV_ALIGN_BOTTOM_MID, 0, -28);

    s_pomo_controls = lv_obj_create(scr);
    lv_obj_set_size(s_pomo_controls, 560, 150);
    lv_obj_align(s_pomo_controls, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_set_style_bg_opa(s_pomo_controls, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_pomo_controls, 0, 0);
    lv_obj_set_style_pad_all(s_pomo_controls, 0, 0);
    lv_obj_remove_flag(s_pomo_controls, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* primary = button(s_pomo_controls, "Pause", 330, 92, ORANGE, on_pomo_primary);
    lv_obj_align(primary, LV_ALIGN_LEFT_MID, 8, 0);
    s_pomo_primary_lbl = lv_obj_get_child(primary, 0);
    lv_obj_t* cancel = button(s_pomo_controls, "End", 180, 92, PANEL, on_pomo_cancel);
    lv_obj_align(cancel, LV_ALIGN_RIGHT_MID, -8, 0);

    pomo_refresh();
    rocky_poll();
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, true);
    ESP_LOGI(TAG, "Rocky Focus screen loaded");
    lvgl_port_unlock();
}

static void sw_screen_deleted(lv_event_t*) {
    s_sw_scr = s_sw_digits = s_sw_primary_lbl = s_sw_laps_lbl = nullptr;
}

static void on_sw_primary(lv_event_t*) {
    if (s_sw_running) {
        s_sw_accum_us += esp_timer_get_time() - s_sw_started_us;
        s_sw_running = false;
    } else {
        s_sw_started_us = esp_timer_get_time();
        s_sw_running = true;
    }
    sw_refresh();
}

static void on_sw_lap(lv_event_t*) {
    if (!s_sw_running) return;
    uint32_t now = sw_elapsed_sec();
    if (s_lap_count < 3) s_laps[s_lap_count++] = now;
    else {
        s_laps[0] = s_laps[1]; s_laps[1] = s_laps[2]; s_laps[2] = now;
    }
    sw_refresh();
}

static void on_sw_reset(lv_event_t*) {
    s_sw_running = false;
    s_sw_started_us = s_sw_accum_us = 0;
    s_lap_count = 0;
    sw_refresh();
}

void focus_show_stopwatch(lv_event_cb_t home_cb) {
    ESP_LOGI(TAG, "opening Stopwatch");
    lvgl_port_lock(0);
    lv_obj_t* scr = lv_obj_create(nullptr);
    style_screen(scr);
    s_sw_scr = scr;
    lv_obj_add_event_cb(scr, sw_screen_deleted, LV_EVENT_DELETE, nullptr);
    add_header(scr, "STOPWATCH", home_cb);

    lv_obj_t* ring;
    if (s_bg_stopwatch.data) {
        ring = lv_image_create(scr);
        lv_image_set_src(ring, &s_bg_stopwatch);
    } else {
        ring = lv_obj_create(scr);
        lv_obj_set_style_bg_color(ring, lv_color_hex(PANEL), 0);
        lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(ring, 7, 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(TEAL), 0);
    }
    lv_obj_set_size(ring, 480, 480);
    lv_obj_align(ring, LV_ALIGN_TOP_MID, 0, 145);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(ring, true, 0);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);

    s_sw_digits = label(ring, "00:00", &lv_font_montserrat_48, TEXT);
    lv_obj_center(s_sw_digits);
    lv_obj_t* helper = label(ring, "elapsed", &lv_font_montserrat_20, MUTED);
    lv_obj_align(helper, LV_ALIGN_CENTER, 0, 75);

    s_sw_laps_lbl = label(scr, "No laps yet", &lv_font_montserrat_20, MUTED);
    lv_obj_set_style_text_align(s_sw_laps_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_sw_laps_lbl, 520);
    lv_obj_align(s_sw_laps_lbl, LV_ALIGN_TOP_MID, 0, 660);

    lv_obj_t* primary = button(scr, "Start", 250, 92, TEAL, on_sw_primary);
    lv_obj_align(primary, LV_ALIGN_BOTTOM_LEFT, 28, -54);
    s_sw_primary_lbl = lv_obj_get_child(primary, 0);
    lv_obj_t* lap = button(scr, "Lap", 130, 92, PURPLE, on_sw_lap);
    lv_obj_align(lap, LV_ALIGN_BOTTOM_MID, 74, -54);
    lv_obj_t* reset = button(scr, "Reset", 130, 92, PANEL, on_sw_reset);
    lv_obj_align(reset, LV_ALIGN_BOTTOM_RIGHT, -28, -54);

    sw_refresh();
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, true);
    ESP_LOGI(TAG, "Stopwatch screen loaded");
    lvgl_port_unlock();
}
