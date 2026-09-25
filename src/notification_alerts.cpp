#include "notification_alerts.h"

#include "alert_protocol.h"
#include "focus_apps.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

namespace {

static const char* TAG = "phone_alerts";
constexpr size_t QUEUE_CAPACITY = 4;
constexpr size_t DEDUPE_CAPACITY = 8;
constexpr int64_t CARD_DURATION_US = 8LL * 1000 * 1000;
constexpr int64_t RATE_WINDOW_US = 30LL * 1000 * 1000;
constexpr uint8_t RATE_MAX = 4;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static PhoneAlert s_queue[QUEUE_CAPACITY] = {};
static size_t s_head = 0;
static size_t s_count = 0;
static uint32_t s_recent_ids[DEDUPE_CAPACITY] = {};
static size_t s_recent_next = 0;
static int64_t s_rate_start_us = 0;
static uint8_t s_rate_count = 0;

// UI state is only accessed from the LVGL service timer.
static lv_obj_t* s_card = nullptr;
static lv_obj_t* s_sender_label = nullptr;
static lv_obj_t* s_meta_label = nullptr;
static bool s_card_visible = false;
static int64_t s_card_until_us = 0;

static bool seen_id(uint32_t id) {
    for (size_t i = 0; i < DEDUPE_CAPACITY; ++i) {
        if (s_recent_ids[i] == id) return true;
    }
    return false;
}

static void remember_id(uint32_t id) {
    s_recent_ids[s_recent_next] = id;
    s_recent_next = (s_recent_next + 1) % DEDUPE_CAPACITY;
}

static bool pop_alert(PhoneAlert* out, bool focus_visible) {
    bool found = false;
    portENTER_CRITICAL(&s_mux);
    size_t eligible = s_count;
    for (size_t i = 0; i < s_count; ++i) {
        size_t pos = (s_head + i) % QUEUE_CAPACITY;
        if (!focus_visible || (s_queue[pos].flags & PHONE_ALERT_FLAG_INTERRUPT_FOCUS)) {
            eligible = i;
            break;
        }
    }
    if (eligible < s_count) {
        size_t pos = (s_head + eligible) % QUEUE_CAPACITY;
        *out = s_queue[pos];
        // Close the small ring gap while retaining arrival order.
        for (size_t i = eligible; i + 1 < s_count; ++i) {
            size_t from = (s_head + i + 1) % QUEUE_CAPACITY;
            size_t to = (s_head + i) % QUEUE_CAPACITY;
            s_queue[to] = s_queue[from];
        }
        --s_count;
        found = true;
    }
    portEXIT_CRITICAL(&s_mux);
    return found;
}

static void dismiss_card(lv_event_t*) {
    if (s_card) {
        lv_obj_add_flag(s_card, LV_OBJ_FLAG_HIDDEN);
    }
    s_card_visible = false;
    s_card_until_us = 0;
}

static void create_card(void) {
    s_card = lv_obj_create(lv_layer_top());
    lv_obj_set_style_bg_color(s_card, lv_color_hex(0x09090C), 0);
    lv_obj_set_style_bg_opa(s_card, 242, 0); // 95%, kept nearly black.
    lv_obj_set_style_border_width(s_card, 2, 0);
    lv_obj_set_style_border_color(s_card, lv_color_hex(0x35C7C2), 0);
    lv_obj_set_style_radius(s_card, 16, 0);
    lv_obj_set_style_pad_left(s_card, 18, 0);
    lv_obj_set_style_pad_right(s_card, 18, 0);
    lv_obj_set_style_pad_top(s_card, 10, 0);
    lv_obj_set_style_pad_bottom(s_card, 8, 0);
    lv_obj_set_style_shadow_width(s_card, 18, 0);
    lv_obj_set_style_shadow_color(s_card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(s_card, LV_OPA_60, 0);
    lv_obj_remove_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_card, dismiss_card, LV_EVENT_CLICKED, nullptr);

    s_sender_label = lv_label_create(s_card);
    lv_label_set_long_mode(s_sender_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_sender_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_sender_label, lv_color_hex(0xFFF7E8), 0);
    lv_obj_align(s_sender_label, LV_ALIGN_TOP_LEFT, 0, 0);

    s_meta_label = lv_label_create(s_card);
    lv_label_set_long_mode(s_meta_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_meta_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_meta_label, lv_color_hex(0x9FDCD8), 0);
    lv_obj_align(s_meta_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_flag(s_card, LV_OBJ_FLAG_HIDDEN);
}

static void show_card(const PhoneAlert& alert) {
    const bool focus_visible = focus_pomodoro_visible();
    const int width = focus_visible ? 440 : 360;
    const int height = focus_visible ? 96 : 112;
    if (!s_card) create_card();

    lv_obj_set_size(s_card, width, height);
    lv_obj_align(s_card, LV_ALIGN_TOP_RIGHT, -20, focus_visible ? 78 : 18);
    lv_obj_set_width(s_sender_label, width - 36);
    lv_label_set_text(s_sender_label, alert.sender);

    char detail[64];
    snprintf(detail, sizeof(detail), "%s  /  %s",
             phone_alert_category_name(alert.category), alert.app);
    lv_obj_set_width(s_meta_label, width - 36);
    lv_label_set_text(s_meta_label, detail);

    lv_obj_remove_flag(s_card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_card);
    s_card_visible = true;
    s_card_until_us = esp_timer_get_time() + CARD_DURATION_US;
}

} // namespace

bool notification_alerts_ingest(const uint8_t* data, size_t len) {
    PhoneAlert alert = {};
    PhoneAlertParseResult parsed = phone_alert_parse(data, len, &alert);
    if (parsed != PHONE_ALERT_PARSE_OK) {
        ESP_LOGW(TAG, "rejected malformed alert (%d, %u bytes)", (int)parsed,
                 (unsigned)len);
        return false;
    }

    bool accepted = false;
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_mux);
    if (!seen_id(alert.event_id)) {
        if (s_rate_start_us == 0 || now - s_rate_start_us >= RATE_WINDOW_US) {
            s_rate_start_us = now;
            s_rate_count = 0;
        }
        if (s_rate_count < RATE_MAX) {
            // A full queue keeps the freshest notifications by dropping its oldest.
            if (s_count == QUEUE_CAPACITY) {
                s_head = (s_head + 1) % QUEUE_CAPACITY;
                --s_count;
            }
            size_t tail = (s_head + s_count) % QUEUE_CAPACITY;
            s_queue[tail] = alert;
            ++s_count;
            ++s_rate_count;
            remember_id(alert.event_id);
            accepted = true;
        }
    }
    portEXIT_CRITICAL(&s_mux);

    if (!accepted) ESP_LOGW(TAG, "dropped duplicate or rate-limited alert");
    return accepted;
}

void notification_alerts_poll(void) {
    const int64_t now = esp_timer_get_time();
    if (s_card_visible && now >= s_card_until_us) dismiss_card(nullptr);
    if (s_card_visible) return;

    PhoneAlert next = {};
    if (pop_alert(&next, focus_pomodoro_visible())) {
        ESP_LOGI(TAG, "showing %s alert from %s", next.app, next.sender);
        show_card(next);
    }
}
