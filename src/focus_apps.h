// focus_apps.h — touch-first Pomodoro and stopwatch apps for the P4 launcher.
#pragma once

#include "lvgl.h"

// Create the shared quarter-second service timer. Call once after LVGL starts.
void focus_apps_init(void);

// Build and load an app screen. `home_cb` is attached to the top-left Home button.
// Timer state survives leaving either screen.
void focus_show_pomodoro(lv_event_cb_t home_cb);
void focus_show_stopwatch(lv_event_cb_t home_cb);

// Used by the launcher for a small live activity dot.
bool focus_pomodoro_running(void);
bool focus_stopwatch_running(void);
