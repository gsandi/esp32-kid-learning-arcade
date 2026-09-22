// Voice notes for the CrowPanel P4's onboard PDM microphone.
#pragma once

#include "lvgl.h"
#include <stdint.h>

// Call once after the first SD card mount attempt. The microphone remains
// ready if storage is absent so a later hot insert can enable recording.
void voice_notes_init(bool sd_available);

// Update storage availability after a hot insert/removal. The microphone and
// recorder task stay initialized so inserting a card does not require reboot.
void voice_notes_set_storage_available(bool sd_available);

// True while a recording or phone transfer has files open on the SD card.
bool voice_notes_storage_busy(void);

// Build the touch-first recorder screen.
void voice_notes_show(lv_event_cb_t home_cb);

// Called on the LVGL service task. Refreshes the recorder UI, finishes saves,
// and starts phone-requested sync work when recording is idle.
void voice_notes_poll(void);

bool voice_notes_available(void);
bool voice_notes_recording(void);

// Existing Rocky app characteristics.
void voice_notes_counts(uint16_t* total, uint16_t* pending_phone);
void voice_notes_request_sync(const char* host, const char* token);
void voice_notes_sync_progress(bool* busy, uint16_t* planned,
                               uint16_t* completed, uint16_t* delivered);
