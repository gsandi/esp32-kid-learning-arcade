// rocky.h — Rocky, the focus buddy: two capsule eyes and an arc mouth that emote.
//
// Ported from pomodoro_esp32_s3 (ui_timer.cpp, Rocky Phase 1 expressions) to LVGL 9 on
// the P4. What came across: NEUTRAL with a random blink every ~2.5 s, LOOK (one deliberate
// blink on a tap), PROUD (squashed happy eyes, big smile, three hops, C5->G5 chirp), and
// the queued-PROUD rule: a block that completes while Rocky is off screen greets you on
// the next return, but only for two minutes, after which the moment has passed.
//
// What did NOT come across, on purpose: DIZZY (shake) and ASLEEP (face-down). Both were
// IMU reactions and this board has no IMU.
//
// Rocky lives inside whatever screen calls rocky_create(); the face is deleted with that
// screen and the module notices through LV_EVENT_DELETE, so nothing here outlives the
// objects it animates.
#pragma once
#include "lvgl.h"

enum RockyExpr {
    ROCKY_NEUTRAL,   // resting: full capsule eyes, gentle smile
    ROCKY_BLINK,     // transient: eyes squash and back
    ROCKY_LOOK,      // tap reaction: one deliberate blink
    ROCKY_PROUD      // session complete: happy eyes, big smile, hops, chirp
};

// Build the face as a child of `parent`, centred at (cx, cy) in parent coordinates.
// scale 1.0 is the original 410-px-wide geometry (72x104 eyes at +/-60); the P4 uses ~1.6.
// Returns the face container. Only one Rocky exists at a time; a second create replaces
// the first's bookkeeping (the old objects belong to their own screen).
lv_obj_t* rocky_create(lv_obj_t* parent, int cx, int cy, float scale);

// Focus uses only the two complete capsule eyes.  Other contexts may keep the
// expressive mouth, so visibility is an explicit presentation choice.
void rocky_set_mouth_visible(bool visible);

// Idempotent. No-op if the face is not on the active screen, except PROUD, which is queued.
void rocky_set_expression(RockyExpr e);

// Ask for PROUD the next time Rocky is visible and resting (2-minute TTL).
void rocky_queue_proud(void);

// Call from the owning screen's periodic tick (or any time): plays a queued PROUD if the
// face is visible and idle. Cheap.
void rocky_poll(void);
