// clock_faces.h — two Orb OS analog clock faces (Aviator + Imperial), stacked on one
// LVGL screen. This is the idle / home surface of the P4 device; the arcade sits one
// tap behind it.
//
// Faces and dial art are ported from Ziplock78/orb-os (MIT, see src/orb/LICENSE.orb-os),
// re-targeted from LVGL 8.4 on a 466x466 round AMOLED to LVGL 9 on a 600x1024 portrait
// MIPI-DSI panel. Each dial keeps its native 466x466 pixels; nothing is rescaled.
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Build the clock screen (not loaded). Allocates the PSRAM canvases and converted hand
// sprites on first call and keeps them for the life of the device: two 466x466 RGB565
// canvases are 868 KB of a 32 MB PSRAM, and freeing/re-allocating on every screen switch
// is churn with no upside on this board.
//
// A 1 Hz LVGL timer redraws both faces while the screen exists; the timer is deleted
// from the screen's LV_EVENT_DELETE, so the caller may load the next screen with
// auto-delete and forget about it.
//
// Returns NULL if the PSRAM allocation fails.
lv_obj_t* clock_faces_build(void);

// Redraw both faces now (e.g. right after NTP sets the time).
void clock_faces_refresh(void);

#ifdef __cplusplus
}
#endif
