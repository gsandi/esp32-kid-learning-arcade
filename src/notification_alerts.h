#pragma once

#include <stddef.h>
#include <stdint.h>

// Safe to call from the Bluetooth callback. Parsing, dedupe and rate limiting
// happen here; LVGL is only touched later by notification_alerts_poll().
bool notification_alerts_ingest(const uint8_t* data, size_t len);

// Must run in LVGL context. Shows queued sender-only cards silently.
void notification_alerts_poll(void);

