// focus_apps.cpp — Rocky Pomodoro + independent stopwatch.
//
// This intentionally ports the useful behavior from pomodoro_esp32_s3, not its
// hardware assumptions: touch controls replace tilt/shake, while Rocky keeps his
// neutral blink, tap reaction and proud completion animation.

#include "focus_apps.h"
#include "app_audio.h"
#include "art_store.h"
#include "rocky.h"
#include "ui_theme.h"
#include "rocky_ble.h"
#include "notification_alerts.h"
#include "voice_notes.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

static const char* TAG = "focus_apps";
static constexpr uint32_t BG       = desk_ui::BLACK;
static constexpr uint32_t PANEL    = desk_ui::SURFACE_RAISED;
static constexpr uint32_t TEXT     = desk_ui::TEXT;
static constexpr uint32_t MUTED    = desk_ui::MUTED;
static constexpr uint32_t ORANGE   = desk_ui::BRASS;
static constexpr uint32_t TEAL     = desk_ui::BRASS;
static constexpr uint32_t GOLD     = desk_ui::BRASS;
static constexpr uint32_t PURPLE   = desk_ui::SURFACE_RAISED;

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
static uint16_t  s_sprint_minutes = 10;
static uint16_t  s_break_minutes = 5;
static uint16_t  s_long_break_minutes = 15;
static uint8_t   s_work_segment = 0;
static uint16_t  s_session_count = 0;
static constexpr uint8_t SESSION_LOG_MAX = 20;
struct SessionEntry {
    uint8_t kind;       // 0 completed, 1 abandoned
    uint8_t hour;
    uint8_t minute;
    uint8_t focused_min;
    uint8_t planned_min;
};
static SessionEntry s_session_log[SESSION_LOG_MAX] = {};
static uint8_t s_session_log_count = 0;
static uint8_t s_focus_start_hour = 0;
static uint8_t s_focus_start_minute = 0;

static bool      s_sw_running = false;
static int64_t   s_sw_started_us = 0;
static int64_t   s_sw_accum_us = 0;
static uint32_t  s_laps[3] = {};
static uint8_t   s_lap_count = 0;

static lv_timer_t* s_service = nullptr;
static lv_image_dsc_t s_bg_stopwatch = {};
static lv_image_dsc_t s_bg_focus_brass = {};
static lv_image_dsc_t s_bg_focus_classic = {};

enum FocusSkin : uint8_t { FOCUS_CLASSIC = 0, FOCUS_BRASS = 1 };
static FocusSkin s_focus_skin = FOCUS_CLASSIC;

// Pomodoro screen refs. They are nulled by the screen DELETE event.
static lv_obj_t* s_pomo_scr = nullptr;
static lv_obj_t* s_pomo_digits = nullptr;
static lv_obj_t* s_pomo_status = nullptr;
static lv_obj_t* s_pomo_hint = nullptr;
static lv_obj_t* s_pomo_presets = nullptr;
static lv_obj_t* s_pomo_controls = nullptr;
static lv_obj_t* s_pomo_primary_lbl = nullptr;
static lv_obj_t* s_pomo_add_btn = nullptr;
static lv_obj_t* s_pomo_skin_image = nullptr;
static lv_obj_t* s_pomo_skin_label = nullptr;
static constexpr uint8_t POMO_TICK_COUNT = 60;
static lv_obj_t* s_pomo_tick_frame = nullptr;
static int8_t s_pomo_last_tick = -1;
static int8_t s_pomo_last_tick_state = -1;

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

bool focus_pomodoro_visible(void) {
    return s_pomo_scr && lv_screen_active() == s_pomo_scr;
}

void focus_ble_get_config(uint16_t out[4]) {
    out[0] = s_focus_minutes;
    out[1] = s_sprint_minutes;
    out[2] = s_break_minutes;
    out[3] = s_long_break_minutes;
}

bool focus_ble_set_config(const uint16_t minutes[4]) {
    if (!minutes) return false;
    for (int i = 0; i < 4; ++i) if (minutes[i] < 1 || minutes[i] > 180) return false;
    s_focus_minutes = minutes[0];
    s_sprint_minutes = minutes[1];
    s_break_minutes = minutes[2];
    s_long_break_minutes = minutes[3];
    if (s_pomo == POMO_READY) s_pomo_total = s_pomo_remaining = (uint32_t)s_focus_minutes * 60;
    nvs_handle_t h;
    if (nvs_open("pomo", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u16(h, "focus", s_focus_minutes);
        nvs_set_u16(h, "sprint", s_sprint_minutes);
        nvs_set_u16(h, "break", s_break_minutes);
        nvs_set_u16(h, "long", s_long_break_minutes);
        nvs_commit(h);
        nvs_close(h);
    }
    return true;
}

void focus_ble_get_state(uint8_t* state, uint8_t* segment, uint16_t* sessions,
                         uint32_t* remaining_sec, uint32_t* total_sec) {
    if (state) *state = (uint8_t)s_pomo; // ordinals 0..6 match the original Rocky app
    if (segment) *segment = s_work_segment;
    if (sessions) *sessions = s_session_count;
    if (remaining_sec) *remaining_sec = s_pomo_remaining;
    if (total_sec) *total_sec = s_pomo_total;
}

size_t focus_ble_get_log(uint8_t* out, size_t capacity) {
    if (!out || capacity == 0) return 0;
    uint8_t count = s_session_log_count;
    size_t max_entries = (capacity - 1) / 5;
    if (count > max_entries) count = (uint8_t)max_entries;
    out[0] = count;
    size_t offset = 1;
    for (uint8_t i = 0; i < count; ++i) {
        const SessionEntry& entry = s_session_log[i];
        out[offset++] = entry.kind;
        out[offset++] = entry.hour;
        out[offset++] = entry.minute;
        out[offset++] = entry.focused_min;
        out[offset++] = entry.planned_min;
    }
    return offset;
}

static uint32_t current_day_key(void) {
    time_t now = time(nullptr);
    if (now < 1577836800) return 0; // Clock not valid yet.
    struct tm local = {};
    localtime_r(&now, &local);
    return (uint32_t)(local.tm_year + 1900) * 1000u + (uint32_t)local.tm_yday;
}

static void capture_focus_start_time(void) {
    time_t now = time(nullptr);
    struct tm local = {};
    if (now >= 1577836800 && localtime_r(&now, &local)) {
        s_focus_start_hour = (uint8_t)local.tm_hour;
        s_focus_start_minute = (uint8_t)local.tm_min;
    } else {
        s_focus_start_hour = 0;
        s_focus_start_minute = 0;
    }
}

static void save_session_log(void) {
    nvs_handle_t h;
    if (nvs_open("pomo_log2", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, "day", current_day_key());
    nvs_set_u8(h, "count", s_session_log_count);
    nvs_set_blob(h, "entries", s_session_log,
                 (size_t)s_session_log_count * sizeof(SessionEntry));
    nvs_commit(h);
    nvs_close(h);
}

static void load_session_log(void) {
    nvs_handle_t h;
    if (nvs_open("pomo_log2", NVS_READWRITE, &h) != ESP_OK) return;
    uint32_t stored_day = 0;
    uint8_t count = 0;
    nvs_get_u32(h, "day", &stored_day);
    nvs_get_u8(h, "count", &count);
    uint32_t today = current_day_key();
    bool same_day = today == 0 || stored_day == 0 || stored_day == today;
    if (same_day && count <= SESSION_LOG_MAX) {
        size_t bytes = (size_t)count * sizeof(SessionEntry);
        if (bytes == 0 || nvs_get_blob(h, "entries", s_session_log, &bytes) == ESP_OK) {
            s_session_log_count = count;
        }
    } else {
        nvs_erase_key(h, "entries");
        nvs_set_u8(h, "count", 0);
        nvs_set_u32(h, "day", today);
        nvs_commit(h);
    }
    nvs_close(h);
    s_session_count = 0;
    for (uint8_t i = 0; i < s_session_log_count; ++i)
        if (s_session_log[i].kind == 0) ++s_session_count;
}

static void add_session_log(bool abandoned, uint32_t focused_sec, uint32_t planned_sec) {
    if (s_session_log_count >= SESSION_LOG_MAX) return;
    SessionEntry& entry = s_session_log[s_session_log_count++];
    entry.kind = abandoned ? 1 : 0;
    entry.hour = s_focus_start_hour;
    entry.minute = s_focus_start_minute;
    entry.focused_min = (uint8_t)((focused_sec / 60) > 255 ? 255 : (focused_sec / 60));
    entry.planned_min = (uint8_t)((planned_sec / 60) > 255 ? 255 : (planned_sec / 60));
    save_session_log();
    ESP_LOGI(TAG, "session logged: %s %02u:%02u %u/%u min",
             abandoned ? "abandoned" : "completed", entry.hour, entry.minute,
             entry.focused_min, entry.planned_min);
}

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
    desk_ui::ButtonKind kind =
        (color == ORANGE || color == TEAL) ? desk_ui::ButtonKind::Primary
                                           : desk_ui::ButtonKind::Secondary;
    lv_obj_t* obj = desk_ui::button(parent, text, w, h, kind, cb, user);
    lv_obj_t* txt = lv_obj_get_child(obj, 0);
    lv_obj_set_style_text_font(txt, &lv_font_montserrat_24, 0);
    return obj;
}

static void add_header(lv_obj_t* scr, const char* title, lv_event_cb_t home_cb) {
    desk_ui::header(scr, title, "ROCKY DESK", home_cb);
}

static void format_clock(char* out, size_t n, uint32_t sec, bool hours) {
    uint32_t h = sec / 3600;
    uint32_t m = (sec / 60) % 60;
    uint32_t s = sec % 60;
    if (hours || h) snprintf(out, n, "%lu:%02lu:%02lu", (unsigned long)h,
                             (unsigned long)m, (unsigned long)s);
    else snprintf(out, n, "%02lu:%02lu", (unsigned long)m, (unsigned long)s);
}

// A full lap is one minute.  The old rectangular countdown frame made Focus
// feel like a draining box.  Sixty radial marks now live in the recessed track
// of the generated brass instrument; one extinguishes each second.
// Tick coordinates are local to the 466x466 instrument object.
static constexpr int FOCUS_CX = 233;
static constexpr int FOCUS_CY = 233;
static constexpr float PI_F = 3.14159265358979323846f;

static uint8_t pomo_tick_elapsed(void) {
    uint32_t seconds = s_pomo_remaining % POMO_TICK_COUNT;
    if (seconds == 0 && s_pomo_remaining > 0) seconds = POMO_TICK_COUNT;
    return (uint8_t)(POMO_TICK_COUNT - seconds);
}

static void draw_pomo_ticks(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
    lv_layer_t* layer = lv_event_get_layer(e);
    uint8_t elapsed = pomo_tick_elapsed();
    bool active = s_pomo != POMO_READY;
    bool paused = pomo_is_paused();
    bool complete = s_pomo == POMO_FOCUS_DONE || s_pomo == POMO_BREAK_DONE;

    for (uint8_t i = 0; i < POMO_TICK_COUNT; ++i) {
        const bool classic = s_focus_skin == FOCUS_CLASSIC;
        const uint32_t active_color = classic ? 0xF4F7FA : 0xFFF0B0;
        const uint32_t dim_color = classic ? 0x4E5964 : 0x6F4718;
        const uint32_t head_color = classic ? 0xD7F1FF : 0xFFFFFF;
        lv_opa_t opa = LV_OPA_60;
        if (complete) opa = LV_OPA_COVER;
        else if (!active) opa = (i % 5 == 0) ? LV_OPA_COVER : LV_OPA_60;
        else if (i >= elapsed) opa = paused ? LV_OPA_70 : LV_OPA_COVER;
        bool head = active && !paused && !complete && elapsed < POMO_TICK_COUNT && i == elapsed;

        const bool major = (i % 5) == 0;
        const float angle = ((float)i * 2.0f * PI_F / POMO_TICK_COUNT) - PI_F / 2.0f;
        const float inner = major ? 176.0f : 181.0f;
        const float outer = 199.0f;

        lv_draw_line_dsc_t d;
        lv_draw_line_dsc_init(&d);
        d.p1.x = (lv_value_precise_t)lroundf(FOCUS_CX + cosf(angle) * inner);
        d.p1.y = (lv_value_precise_t)lroundf(FOCUS_CY + sinf(angle) * inner);
        d.p2.x = (lv_value_precise_t)lroundf(FOCUS_CX + cosf(angle) * outer);
        d.p2.y = (lv_value_precise_t)lroundf(FOCUS_CY + sinf(angle) * outer);
        d.color = lv_color_hex(complete ? active_color
                                       : (head ? head_color
                                               : (i >= elapsed ? active_color : dim_color)));
        d.opa = opa;
        d.width = major ? 7 : 4;
        d.round_start = 1;
        d.round_end = 1;
        lv_draw_line(layer, &d);
    }
}

static void create_pomo_ticks(lv_obj_t* parent) {
    s_pomo_tick_frame = lv_obj_create(parent);
    lv_obj_set_size(s_pomo_tick_frame, 466, 466);
    lv_obj_set_pos(s_pomo_tick_frame, 25, 90);
    lv_obj_set_style_bg_opa(s_pomo_tick_frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_pomo_tick_frame, 0, 0);
    lv_obj_set_style_pad_all(s_pomo_tick_frame, 0, 0);
    lv_obj_remove_flag(s_pomo_tick_frame, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_pomo_tick_frame, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_pomo_tick_frame, draw_pomo_ticks, LV_EVENT_DRAW_MAIN, nullptr);
    s_pomo_last_tick = -1;
    s_pomo_last_tick_state = -1;
}

static void refresh_pomo_ticks(void) {
    if (!s_pomo_tick_frame) return;
    int8_t elapsed = (int8_t)pomo_tick_elapsed();
    int8_t state = (int8_t)s_pomo;
    if (s_pomo_last_tick_state != state || s_pomo_last_tick != elapsed)
        lv_obj_invalidate(s_pomo_tick_frame);
    s_pomo_last_tick = elapsed;
    s_pomo_last_tick_state = state;
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
    if (s_pomo_add_btn) {
        bool focus_active = s_pomo == POMO_FOCUS_RUNNING || s_pomo == POMO_FOCUS_PAUSED;
        if (focus_active) lv_obj_remove_flag(s_pomo_add_btn, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_pomo_add_btn, LV_OBJ_FLAG_HIDDEN);
    }

    bool ready = s_pomo == POMO_READY;
    if (ready) {
        lv_obj_remove_flag(s_pomo_presets, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_pomo_controls, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_pomo_presets, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_pomo_controls, LV_OBJ_FLAG_HIDDEN);
    }
    refresh_pomo_ticks();
}

static void pomo_complete(void) {
    if (s_pomo == POMO_FOCUS_RUNNING) {
        add_session_log(false, s_pomo_total, s_pomo_total);
        s_pomo = POMO_FOCUS_DONE;
        ++s_session_count;
    }
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
    rocky_ble_poll();
    notification_alerts_poll();
    voice_notes_poll();
}

void focus_apps_init(void) {
    nvs_handle_t h;
    uint16_t saved[4] = {s_focus_minutes, s_sprint_minutes, s_break_minutes, s_long_break_minutes};
    if (nvs_open("pomo", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u16(h, "focus", &saved[0]);
        nvs_get_u16(h, "sprint", &saved[1]);
        nvs_get_u16(h, "break", &saved[2]);
        nvs_get_u16(h, "long", &saved[3]);
        uint8_t skin = FOCUS_CLASSIC;
        if (nvs_get_u8(h, "skin", &skin) == ESP_OK && skin <= FOCUS_BRASS)
            s_focus_skin = (FocusSkin)skin;
        nvs_close(h);
        bool valid = true;
        for (int i = 0; i < 4; ++i) valid &= saved[i] >= 1 && saved[i] <= 180;
        if (valid) {
            s_focus_minutes = saved[0];
            s_sprint_minutes = saved[1];
            s_break_minutes = saved[2];
            s_long_break_minutes = saved[3];
            s_pomo_total = s_pomo_remaining = (uint32_t)s_focus_minutes * 60;
        }
    }
    load_session_log();
    art_image("bg_stopwatch", &s_bg_stopwatch);
    art_image("bg_focus", &s_bg_focus_brass);
    art_image("bg_focus_clean", &s_bg_focus_classic);
    if (!s_service) s_service = lv_timer_create(service_cb, 250, nullptr);
}

static void pomo_screen_deleted(lv_event_t*) {
    s_pomo_scr = s_pomo_digits = s_pomo_status = s_pomo_hint = nullptr;
    s_pomo_presets = s_pomo_controls = s_pomo_primary_lbl = s_pomo_add_btn = nullptr;
    s_pomo_tick_frame = nullptr;
    s_pomo_skin_image = s_pomo_skin_label = nullptr;
    s_pomo_last_tick = s_pomo_last_tick_state = -1;
}

static const lv_image_dsc_t* active_focus_skin() {
    if (s_focus_skin == FOCUS_CLASSIC && s_bg_focus_classic.data)
        return &s_bg_focus_classic;
    return s_bg_focus_brass.data ? &s_bg_focus_brass : nullptr;
}

static void on_focus_skin_toggle(lv_event_t*) {
    s_focus_skin = s_focus_skin == FOCUS_CLASSIC ? FOCUS_BRASS : FOCUS_CLASSIC;
    nvs_handle_t h;
    if (nvs_open("pomo", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "skin", (uint8_t)s_focus_skin);
        nvs_commit(h);
        nvs_close(h);
    }
    const lv_image_dsc_t* skin = active_focus_skin();
    if (s_pomo_skin_image && skin) lv_image_set_src(s_pomo_skin_image, skin);
    if (s_pomo_skin_label)
        lv_label_set_text(s_pomo_skin_label,
                          s_focus_skin == FOCUS_CLASSIC ? "CLASSIC" : "BRASS");
    s_pomo_last_tick = s_pomo_last_tick_state = -1;
    refresh_pomo_ticks();
}

static void on_rocky_tap(lv_event_t*) { rocky_set_expression(ROCKY_LOOK); }

static void on_preset(lv_event_t* e) {
    s_focus_minutes = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    s_pomo_total = s_pomo_remaining = (uint32_t)s_focus_minutes * 60;
    s_pomo_deadline_us = esp_timer_get_time() + (int64_t)s_pomo_remaining * 1000000;
    s_pomo = POMO_FOCUS_RUNNING;
    ++s_work_segment;
    capture_focus_start_time();
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
        s_pomo_total = s_pomo_remaining = (uint32_t)s_break_minutes * 60;
        s_pomo_deadline_us = esp_timer_get_time() + (int64_t)s_pomo_remaining * 1000000;
        s_pomo = POMO_BREAK_RUNNING;
    } else if (s_pomo == POMO_BREAK_DONE) {
        s_pomo_total = s_pomo_remaining = (uint32_t)s_focus_minutes * 60;
        s_pomo = POMO_READY;
    }
    pomo_refresh();
}

static void on_pomo_cancel(lv_event_t*) {
    if (s_pomo == POMO_FOCUS_RUNNING) pomo_update_model();
    if (s_pomo == POMO_FOCUS_RUNNING || s_pomo == POMO_FOCUS_PAUSED) {
        uint32_t focused = s_pomo_total > s_pomo_remaining
                         ? s_pomo_total - s_pomo_remaining : 0;
        add_session_log(true, focused, s_pomo_total);
    }
    s_pomo = POMO_READY;
    s_pomo_total = s_pomo_remaining = (uint32_t)s_focus_minutes * 60;
    pomo_refresh();
}

static void on_pomo_add_five(lv_event_t*) {
    if (s_pomo != POMO_FOCUS_RUNNING && s_pomo != POMO_FOCUS_PAUSED) return;
    if (s_pomo == POMO_FOCUS_RUNNING) pomo_update_model();
    constexpr uint32_t FIVE_MIN = 5 * 60;
    constexpr uint32_t MAX_FOCUS = 180 * 60;
    if (s_pomo_total > MAX_FOCUS - FIVE_MIN) return;
    s_pomo_total += FIVE_MIN;
    s_pomo_remaining += FIVE_MIN;
    if (s_pomo == POMO_FOCUS_RUNNING) s_pomo_deadline_us += (int64_t)FIVE_MIN * 1000000;
    ESP_LOGI(TAG, "focus extended by five minutes; total=%lu sec",
             (unsigned long)s_pomo_total);
    pomo_refresh();
}

void focus_show_pomodoro(lv_event_cb_t home_cb) {
    ESP_LOGI(TAG, "opening Rocky Focus");
    pomo_update_model();
    lvgl_port_lock(0);
    lv_obj_t* scr = lv_obj_create(nullptr);
    style_screen(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    s_pomo_scr = scr;
    lv_obj_add_event_cb(scr, pomo_screen_deleted, LV_EVENT_DELETE, nullptr);
    add_header(scr, "FOCUS", home_cb);

    const lv_image_dsc_t* skin = active_focus_skin();
    if (skin) {
        s_pomo_skin_image = lv_image_create(scr);
        lv_image_set_src(s_pomo_skin_image, skin);
        lv_obj_set_pos(s_pomo_skin_image, 25, 90);
        desk_ui::make_inert(s_pomo_skin_image);
    }
    create_pomo_ticks(scr);

    // Deliberately quiet: gaze, countdown, progress, controls. Nothing else.
    // Instrument and character occupy the left half; time and controls own the
    // right half.  The split remains stable while the session is running.
    lv_obj_t* face = rocky_create(scr, 258, 378, 0.82f);
    rocky_set_mouth_visible(true);
    lv_obj_add_flag(face, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(face, on_rocky_tap, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* split = lv_obj_create(scr);
    lv_obj_set_size(split, 1, 444);
    lv_obj_set_pos(split, 512, 96);
    lv_obj_set_style_bg_color(split, lv_color_hex(desk_ui::LINE), 0);
    lv_obj_set_style_bg_opa(split, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(split, 0, 0);
    lv_obj_set_style_pad_all(split, 0, 0);
    desk_ui::make_inert(split);

    // Rocky, status and countdown are one instrument again. The right half is
    // reserved for choices and in-session controls rather than duplicating time.
    s_pomo_status = label(scr, "", &lv_font_montserrat_20, TEXT);
    lv_obj_set_width(s_pomo_status, 300);
    lv_obj_set_style_text_align(s_pomo_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_pomo_status, 108, 166);
    s_pomo_digits = label(scr, "25:00", &lv_font_montserrat_48, TEXT);
    lv_obj_set_width(s_pomo_digits, 260);
    lv_obj_set_style_text_align(s_pomo_digits, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_pomo_digits, 128, 416);
    s_pomo_hint = label(scr, "", &lv_font_montserrat_20, MUTED);
    lv_obj_set_width(s_pomo_hint, 430);
    lv_obj_set_style_text_align(s_pomo_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_pomo_hint, LV_ALIGN_TOP_LEFT, 552, 154);

    lv_obj_t* skin_button = desk_ui::button(scr,
        s_focus_skin == FOCUS_CLASSIC ? "CLASSIC" : "BRASS",
        136, 48, desk_ui::ButtonKind::Secondary, on_focus_skin_toggle, nullptr);
    lv_obj_align(skin_button, LV_ALIGN_TOP_RIGHT, -18, 13);
    s_pomo_skin_label = lv_obj_get_child(skin_button, 0);

    s_pomo_presets = lv_obj_create(scr);
    lv_obj_set_size(s_pomo_presets, 480, 220);
    lv_obj_align(s_pomo_presets, LV_ALIGN_BOTTOM_RIGHT, -18, -38);
    lv_obj_set_style_bg_opa(s_pomo_presets, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_pomo_presets, 0, 0);
    lv_obj_set_style_pad_all(s_pomo_presets, 0, 0);
    lv_obj_remove_flag(s_pomo_presets, LV_OBJ_FLAG_SCROLLABLE);
    const uint16_t mins[] = {s_sprint_minutes, s_focus_minutes, 45};
    char names[3][12];
    for (int i = 0; i < 3; ++i) snprintf(names[i], sizeof(names[i]), "%u min", mins[i]);
    for (int i = 0; i < 3; ++i) {
        lv_obj_t* b = button(s_pomo_presets, names[i], 142, 88, i == 1 ? ORANGE : PANEL,
                             on_preset, (void*)(uintptr_t)mins[i]);
        lv_obj_set_pos(b, 8 + i * 158, 24);
    }
    lv_obj_t* preset_hint = label(s_pomo_presets, "SPRINT            STANDARD              DEEP",
                                  &lv_font_montserrat_18, MUTED);
    lv_obj_align(preset_hint, LV_ALIGN_BOTTOM_MID, 0, -28);

    s_pomo_controls = lv_obj_create(scr);
    lv_obj_set_size(s_pomo_controls, 480, 150);
    lv_obj_align(s_pomo_controls, LV_ALIGN_BOTTOM_RIGHT, -18, -54);
    lv_obj_set_style_bg_opa(s_pomo_controls, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_pomo_controls, 0, 0);
    lv_obj_set_style_pad_all(s_pomo_controls, 0, 0);
    lv_obj_remove_flag(s_pomo_controls, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* primary = button(s_pomo_controls, "Pause", 190, 88, ORANGE, on_pomo_primary);
    lv_obj_align(primary, LV_ALIGN_LEFT_MID, 8, 0);
    s_pomo_primary_lbl = lv_obj_get_child(primary, 0);
    s_pomo_add_btn = button(s_pomo_controls, "+5 min", 126, 88, PURPLE, on_pomo_add_five);
    lv_obj_align(s_pomo_add_btn, LV_ALIGN_CENTER, 30, 0);
    lv_obj_t* cancel = button(s_pomo_controls, "End", 126, 88, PANEL, on_pomo_cancel);
    lv_obj_align(cancel, LV_ALIGN_RIGHT_MID, -8, 0);

    pomo_refresh();
    rocky_poll();
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
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
    lv_obj_align(ring, LV_ALIGN_BOTTOM_LEFT, 16, -18);
    // Large clip-corner layers exhaust LVGL's 64 KB draw heap. The generated
    // instrument already draws its own circular bezel, so it needs no mask.
    if (!s_bg_stopwatch.data) lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);

    s_sw_digits = label(ring, "00:00", &lv_font_montserrat_48, TEXT);
    lv_obj_center(s_sw_digits);
    lv_obj_t* helper = label(ring, "elapsed", &lv_font_montserrat_20, MUTED);
    lv_obj_align(helper, LV_ALIGN_CENTER, 0, 75);

    s_sw_laps_lbl = label(scr, "No laps yet", &lv_font_montserrat_20, MUTED);
    lv_obj_set_style_text_align(s_sw_laps_lbl, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_width(s_sw_laps_lbl, 430);
    lv_obj_align(s_sw_laps_lbl, LV_ALIGN_TOP_LEFT, 548, 156);

    lv_obj_t* primary = button(scr, "Start", 210, 88, TEAL, on_sw_primary);
    lv_obj_align(primary, LV_ALIGN_BOTTOM_LEFT, 530, -62);
    s_sw_primary_lbl = lv_obj_get_child(primary, 0);
    lv_obj_t* lap = button(scr, "Lap", 112, 88, PANEL, on_sw_lap);
    lv_obj_align(lap, LV_ALIGN_BOTTOM_LEFT, 752, -62);
    lv_obj_t* reset = button(scr, "Reset", 112, 88, PANEL, on_sw_reset);
    lv_obj_align(reset, LV_ALIGN_BOTTOM_RIGHT, -36, -62);

    sw_refresh();
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    ESP_LOGI(TAG, "Stopwatch screen loaded");
    lvgl_port_unlock();
}
