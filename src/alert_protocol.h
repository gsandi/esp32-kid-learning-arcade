#pragma once

#include <stddef.h>
#include <stdint.h>

// Sender-only phone alert protocol carried by the 6b1d000d characteristic.
// Message bodies are intentionally not part of this structure.
enum PhoneAlertCategory : uint8_t {
    PHONE_ALERT_OTHER = 0,
    PHONE_ALERT_CALL = 1,
    PHONE_ALERT_MESSAGE = 2,
    PHONE_ALERT_CALENDAR = 3,
    PHONE_ALERT_REMINDER = 4,
    PHONE_ALERT_ALARM = 5,
};

constexpr uint8_t PHONE_ALERT_FLAG_INTERRUPT_FOCUS = 1u << 0;
constexpr uint8_t PHONE_ALERT_FLAG_PASS_DND = 1u << 1;

struct PhoneAlert {
    uint32_t event_id;
    uint8_t flags;
    PhoneAlertCategory category;
    char app[25];       // 24 UTF-8 bytes plus NUL
    char sender[64];    // 63 UTF-8 bytes plus NUL
};

enum PhoneAlertParseResult {
    PHONE_ALERT_PARSE_OK = 0,
    PHONE_ALERT_PARSE_NULL,
    PHONE_ALERT_PARSE_LENGTH,
    PHONE_ALERT_PARSE_VERSION,
    PHONE_ALERT_PARSE_FLAGS,
    PHONE_ALERT_PARSE_CATEGORY,
    PHONE_ALERT_PARSE_EVENT_ID,
    PHONE_ALERT_PARSE_UTF8,
};

// Version 1 wire format (all lengths are bytes):
//   version:u8 (=1), flags:u8, category:u8, app_len:u8, sender_len:u8,
//   event_id:u32 little-endian, app:utf8, sender:utf8
// The total packet must exactly match the two declared string lengths.
PhoneAlertParseResult phone_alert_parse(const uint8_t* data, size_t len,
                                        PhoneAlert* out);

const char* phone_alert_category_name(PhoneAlertCategory category);
