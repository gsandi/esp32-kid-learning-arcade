// clock_faces.cpp — embossed local + India instrument faces, side by side.
//
// The dial plates are immutable images mapped directly from the art partition.
// Moving hands are lightweight LVGL line objects. Do not redraw both 466x466
// canvases every second: on the P4 that can keep taskLVGL permanently busy and
// starve IDLE0 until the task watchdog fires.

#include "clock_faces.h"
#include "art_store.h"
#include "esp_log.h"
#include <math.h>
#include <time.h>

static const char* TAG = "clock_faces";

static constexpr int DIAL_W = 466;
static constexpr int DIAL_H = 466;
static constexpr int DIAL_LEFT_X = 30;
static constexpr int DIAL_RIGHT_X = DIAL_LEFT_X + DIAL_W + 32;
static constexpr int DIAL_Y = 67;
static constexpr float CX = 233.0f;
static constexpr float CY = 233.0f;
static constexpr float DEG2RAD = 3.14159265358979f / 180.0f;

struct Hand {
    lv_obj_t* edge = nullptr;
    lv_obj_t* fill = nullptr;
    lv_point_precise_t edge_pts[2]{};
    lv_point_precise_t fill_pts[2]{};
};

struct Face {
    lv_obj_t* root = nullptr;
    lv_obj_t* date = nullptr;
    Hand hour;
    Hand minute;
    Hand second;
    lv_image_dsc_t dial{};
};

static lv_obj_t* s_screen = nullptr;
static lv_timer_t* s_tick = nullptr;
static Face s_local;
static Face s_india;
static bool s_no_time = false;

static lv_point_precise_t point_at(float angle_deg, float radius) {
    const float a = angle_deg * DEG2RAD;
    return {(lv_value_precise_t)lroundf(CX + radius * sinf(a)),
            (lv_value_precise_t)lroundf(CY - radius * cosf(a))};
}

static lv_obj_t* make_line(lv_obj_t* parent, lv_color_t color, int width,
                           lv_point_precise_t points[2]) {
    lv_obj_t* line = lv_line_create(parent);
    lv_line_set_points(line, points, 2);
    lv_obj_set_style_line_color(line, color, 0);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    return line;
}

static void make_hand(lv_obj_t* parent, Hand& hand, lv_color_t fill,
                      lv_color_t edge, int width) {
    hand.edge_pts[0] = hand.edge_pts[1] = {(lv_value_precise_t)CX, (lv_value_precise_t)CY};
    hand.fill_pts[0] = hand.fill_pts[1] = {(lv_value_precise_t)CX, (lv_value_precise_t)CY};
    hand.edge = make_line(parent, edge, width + 3, hand.edge_pts);
    hand.fill = make_line(parent, fill, width, hand.fill_pts);
}

static void set_hand(Hand& hand, float angle_deg, float length, float tail) {
    const lv_point_precise_t start = point_at(angle_deg, -tail);
    const lv_point_precise_t end = point_at(angle_deg, length);
    hand.edge_pts[0] = hand.fill_pts[0] = start;
    hand.edge_pts[1] = hand.fill_pts[1] = end;
    lv_line_set_points(hand.edge, hand.edge_pts, 2);
    lv_line_set_points(hand.fill, hand.fill_pts, 2);
}

static lv_obj_t* make_disc(lv_obj_t* parent, int diameter, lv_color_t color) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_set_size(obj, diameter, diameter);
    lv_obj_set_pos(obj, (int)CX - diameter / 2, (int)CY - diameter / 2);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static void time_for_faces(struct tm* local, struct tm* india) {
    const time_t now = time(nullptr);
    localtime_r(&now, local);
    const time_t india_now = now + (5 * 60 + 30) * 60;
    gmtime_r(&india_now, india);
    s_no_time = now <= 1704067200L;
    if (s_no_time) {
        local->tm_hour = india->tm_hour = 0;
        local->tm_min = india->tm_min = 0;
    }
}

static void update_face(Face& face, const struct tm& ti) {
    const float sec = ti.tm_sec;
    const float mins = ti.tm_min + sec / 60.0f;
    const float hrs = (ti.tm_hour % 12) + mins / 60.0f;
    set_hand(face.hour, hrs * 30.0f, 116, 20);
    set_hand(face.minute, mins * 6.0f, 190, 26);
    set_hand(face.second, sec * 6.0f, 196, 48);
    if (s_no_time) lv_label_set_text(face.date, "--");
    else lv_label_set_text_fmt(face.date, "%d", ti.tm_mday);
}

static void redraw_all() {
    if (!s_screen || lv_screen_active() != s_screen) return;
    struct tm local{}, india{};
    time_for_faces(&local, &india);
    update_face(s_local, local);
    update_face(s_india, india);
}

static void tick_cb(lv_timer_t*) { redraw_all(); }

void clock_faces_refresh(void) { redraw_all(); }

static void on_screen_delete(lv_event_t*) {
    if (s_tick) {
        lv_timer_delete(s_tick);
        s_tick = nullptr;
    }
    s_screen = nullptr;
    s_local = Face{};
    s_india = Face{};
}

static void build_face(Face& face, int x, const char* art_name, const char* zone,
                       lv_color_t fill, lv_color_t edge, lv_color_t seconds,
                       lv_color_t date_color) {
    face.root = lv_obj_create(s_screen);
    lv_obj_set_size(face.root, DIAL_W, DIAL_H);
    lv_obj_set_pos(face.root, x, DIAL_Y);
    lv_obj_set_style_bg_color(face.root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(face.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(face.root, 0, 0);
    lv_obj_set_style_pad_all(face.root, 0, 0);
    lv_obj_remove_flag(face.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(face.root, LV_OBJ_FLAG_CLICKABLE);

    if (art_image(art_name, &face.dial)) {
        lv_obj_t* image = lv_image_create(face.root);
        lv_image_set_src(image, &face.dial);
        lv_obj_set_pos(image, 0, 0);
        lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
    } else {
        ESP_LOGE(TAG, "missing %s art; using black dial", art_name);
    }

    lv_obj_t* zone_label = lv_label_create(face.root);
    lv_label_set_text(zone_label, zone);
    lv_obj_set_style_text_font(zone_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(zone_label, fill, 0);
    lv_obj_set_width(zone_label, 180);
    lv_obj_set_style_text_align(zone_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(zone_label, 143, 112);

    face.date = lv_label_create(face.root);
    lv_obj_set_width(face.date, 48);
    lv_obj_set_style_text_align(face.date, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(face.date, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(face.date, date_color, 0);
    lv_obj_set_pos(face.date, 209, 377);

    make_hand(face.root, face.hour, fill, edge, 14);
    make_hand(face.root, face.minute, fill, edge, 11);
    make_hand(face.root, face.second, seconds, edge, 3);
    make_disc(face.root, 18, edge);
    make_disc(face.root, 10, fill);
}

lv_obj_t* clock_faces_build(void) {
    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_screen, on_screen_delete, LV_EVENT_DELETE, nullptr);

    build_face(s_local, DIAL_LEFT_X, "dial_local", "LOCAL",
               lv_color_hex(0xF1F4F7), lv_color_hex(0x162335), lv_color_hex(0xB23A2C),
               lv_color_hex(0xF2F5F9));
    build_face(s_india, DIAL_RIGHT_X, "dial_steam", "INDIA  IST",
               lv_color_hex(0xE6B858), lv_color_hex(0x2B1608), lv_color_hex(0xE6B858),
               lv_color_hex(0xF2F5F9));

    redraw_all();
    s_tick = lv_timer_create(tick_cb, 1000, nullptr);
    return s_screen;
}
