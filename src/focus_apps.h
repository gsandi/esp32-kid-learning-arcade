// focus_apps.h — touch-first Pomodoro and stopwatch apps for the P4 launcher.
#pragma once

#include "lvgl.h"
#include <stddef.h>

// Create the shared quarter-second service timer. Call once after LVGL starts.
void focus_apps_init(void);

// Build and load an app screen. `home_cb` is attached to the top-left Home button.
// Timer state survives leaving either screen.
void focus_show_pomodoro(lv_event_cb_t home_cb);
void focus_show_stopwatch(lv_event_cb_t home_cb);

// Used by the launcher for a small live activity dot.
bool focus_pomodoro_running(void);
bool focus_stopwatch_running(void);

// Existing Rocky companion-app protocol. Durations are focus, sprint, break and
// long break in minutes. State bytes use the original PomodoroState ordinals.
void focus_ble_get_config(uint16_t out_minutes[4]);
bool focus_ble_set_config(const uint16_t minutes[4]);
void focus_ble_get_state(uint8_t* state, uint8_t* segment, uint16_t* sessions,
                         uint32_t* remaining_sec, uint32_t* total_sec);

// Existing Rocky app LOG wire format: count byte followed by up to twenty
// 5-byte entries (kind, start hour, start minute, focused min, planned min).
size_t focus_ble_get_log(uint8_t* out, size_t capacity);
