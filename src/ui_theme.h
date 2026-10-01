#pragma once

#include "lvgl.h"

// Shared visual language for the 1024x600 Rocky Desk UI.  Keep this module
// allocation-light: every helper builds ordinary LVGL objects and owns no
// persistent buffers, so screens can still be deleted normally.
namespace desk_ui {

constexpr int SCREEN_W = 1024;
constexpr int SCREEN_H = 600;
constexpr int HEADER_H = 72;
constexpr int EDGE = 20;

constexpr uint32_t BLACK          = 0x000000;
constexpr uint32_t SURFACE        = 0x0D0D0F;
constexpr uint32_t SURFACE_RAISED = 0x17171A;
constexpr uint32_t SURFACE_PRESS  = 0x242429;
constexpr uint32_t LINE           = 0x303036;
constexpr uint32_t TEXT           = 0xF4F1E8;
constexpr uint32_t MUTED          = 0x98959D;
constexpr uint32_t BRASS          = 0xD6A84B;
constexpr uint32_t BRASS_DIM      = 0x80632E;
constexpr uint32_t SUCCESS        = 0x4EBB88;
constexpr uint32_t INFO           = 0x5B9FB8;
constexpr uint32_t DANGER         = 0xC85B66;

enum class ButtonKind {
    Primary,
    Secondary,
    Quiet,
    Danger,
};

lv_obj_t* screen();
lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font,
                uint32_t color = TEXT);
lv_obj_t* panel(lv_obj_t* parent, int x, int y, int width, int height,
                bool outlined = true);
lv_obj_t* button(lv_obj_t* parent, const char* text, int width, int height,
                 ButtonKind kind, lv_event_cb_t callback,
                 void* user_data = nullptr);
lv_obj_t* header(lv_obj_t* screen, const char* title, const char* eyebrow,
                 lv_event_cb_t back_callback, void* user_data = nullptr);
lv_obj_t* status_dot(lv_obj_t* parent, uint32_t color, int diameter = 12);
lv_obj_t* section_label(lv_obj_t* parent, const char* text);
void set_status_dot(lv_obj_t* dot, uint32_t color, bool active);
void make_inert(lv_obj_t* object);

}  // namespace desk_ui
