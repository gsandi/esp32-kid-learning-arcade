#include "ui_theme.h"

namespace desk_ui {

void make_inert(lv_obj_t* object) {
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
}

lv_obj_t* screen() {
    lv_obj_t* object = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(object, lv_color_hex(BLACK), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}

lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font,
                uint32_t color) {
    lv_obj_t* object = lv_label_create(parent);
    lv_label_set_text(object, text);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    return object;
}

lv_obj_t* panel(lv_obj_t* parent, int x, int y, int width, int height,
                bool outlined) {
    lv_obj_t* object = lv_obj_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_bg_color(object, lv_color_hex(SURFACE), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(object, 18, 0);
    lv_obj_set_style_border_width(object, outlined ? 1 : 0, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(LINE), 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}

lv_obj_t* button(lv_obj_t* parent, const char* text, int width, int height,
                 ButtonKind kind, lv_event_cb_t callback, void* user_data) {
    uint32_t background = SURFACE_RAISED;
    uint32_t foreground = TEXT;
    uint32_t border = LINE;
    int border_width = 1;
    if (kind == ButtonKind::Primary) {
        background = BRASS;
        foreground = BLACK;
        border = BRASS;
        border_width = 0;
    } else if (kind == ButtonKind::Danger) {
        background = SURFACE_RAISED;
        foreground = DANGER;
        border = DANGER;
    } else if (kind == ButtonKind::Quiet) {
        background = BLACK;
        foreground = MUTED;
    }

    lv_obj_t* object = lv_button_create(parent);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_bg_color(object, lv_color_hex(background), 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(SURFACE_PRESS), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(object, lv_color_hex(foreground), 0);
    lv_obj_set_style_radius(object, 14, 0);
    lv_obj_set_style_border_width(object, border_width, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(border), 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    // Color-only press feedback needs no offscreen transform layer. Large
    // controls can otherwise exhaust LVGL's fixed heap and stall rendering.
    if (callback) lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, user_data);

    lv_obj_t* caption = label(object, text, &lv_font_montserrat_20, foreground);
    lv_obj_center(caption);
    return object;
}

lv_obj_t* header(lv_obj_t* parent, const char* title, const char* eyebrow,
                 lv_event_cb_t back_callback, void* user_data) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, SCREEN_W, HEADER_H);
    lv_obj_set_style_bg_color(bar, lv_color_hex(BLACK), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(LINE), 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    if (back_callback) {
        lv_obj_t* back = button(bar, "<", 56, 48, ButtonKind::Quiet,
                                back_callback, user_data);
        lv_obj_align(back, LV_ALIGN_LEFT_MID, 14, 0);
        lv_obj_t* caption = lv_obj_get_child(back, 0);
        lv_obj_set_style_text_font(caption, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(caption, lv_color_hex(TEXT), 0);
    }

    lv_obj_t* heading = label(bar, title, &lv_font_montserrat_28, TEXT);
    lv_obj_align(heading, LV_ALIGN_LEFT_MID, back_callback ? 86 : 20, 7);
    if (eyebrow && eyebrow[0]) {
        lv_obj_t* small = label(bar, eyebrow, &lv_font_montserrat_18, BRASS);
        lv_obj_align(small, LV_ALIGN_LEFT_MID, back_callback ? 88 : 22, -20);
    }
    return bar;
}

lv_obj_t* status_dot(lv_obj_t* parent, uint32_t color, int diameter) {
    lv_obj_t* dot = lv_obj_create(parent);
    lv_obj_set_size(dot, diameter, diameter);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_pad_all(dot, 0, 0);
    make_inert(dot);
    return dot;
}

void set_status_dot(lv_obj_t* dot, uint32_t color, bool active) {
    if (!dot) return;
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(dot, active ? LV_OPA_COVER : LV_OPA_50, 0);
    lv_obj_set_style_shadow_width(dot, active ? 8 : 0, 0);
    lv_obj_set_style_shadow_color(dot, lv_color_hex(color), 0);
    lv_obj_set_style_shadow_opa(dot, active ? LV_OPA_50 : LV_OPA_TRANSP, 0);
}

lv_obj_t* section_label(lv_obj_t* parent, const char* text) {
    lv_obj_t* object = label(parent, text, &lv_font_montserrat_18, BRASS);
    lv_obj_set_style_text_letter_space(object, 2, 0);
    return object;
}

}  // namespace desk_ui
