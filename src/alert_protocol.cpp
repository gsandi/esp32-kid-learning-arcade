#include "alert_protocol.h"

#include <string.h>

namespace {

constexpr size_t HEADER_LEN = 9;
constexpr size_t APP_MAX = 24;
constexpr size_t SENDER_MAX = 63;

static bool valid_utf8_text(const uint8_t* s, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint32_t cp;
        uint8_t c = s[i++];
        if (c < 0x80) {
            cp = c;
        } else if (c >= 0xC2 && c <= 0xDF) {
            if (i >= len || (s[i] & 0xC0) != 0x80) return false;
            cp = ((uint32_t)(c & 0x1F) << 6) | (s[i++] & 0x3F);
        } else if (c >= 0xE0 && c <= 0xEF) {
            if (i + 1 >= len || (s[i] & 0xC0) != 0x80 ||
                (s[i + 1] & 0xC0) != 0x80) return false;
            uint8_t c1 = s[i];
            if ((c == 0xE0 && c1 < 0xA0) || (c == 0xED && c1 >= 0xA0)) return false;
            cp = ((uint32_t)(c & 0x0F) << 12) |
                 ((uint32_t)(s[i] & 0x3F) << 6) | (s[i + 1] & 0x3F);
            i += 2;
        } else if (c >= 0xF0 && c <= 0xF4) {
            if (i + 2 >= len || (s[i] & 0xC0) != 0x80 ||
                (s[i + 1] & 0xC0) != 0x80 || (s[i + 2] & 0xC0) != 0x80) return false;
            uint8_t c1 = s[i];
            if ((c == 0xF0 && c1 < 0x90) || (c == 0xF4 && c1 >= 0x90)) return false;
            cp = ((uint32_t)(c & 0x07) << 18) |
                 ((uint32_t)(s[i] & 0x3F) << 12) |
                 ((uint32_t)(s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
            i += 3;
        } else {
            return false;
        }

        // Labels must be one line and must not contain invisible C0/C1 controls.
        if (cp == 0 || cp == '\n' || cp == '\r' || cp == '\t' ||
            cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) return false;
    }
    return true;
}

static uint32_t get32(const uint8_t* b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

} // namespace

PhoneAlertParseResult phone_alert_parse(const uint8_t* data, size_t len,
                                        PhoneAlert* out) {
    if (!data || !out) return PHONE_ALERT_PARSE_NULL;
    if (len < HEADER_LEN) return PHONE_ALERT_PARSE_LENGTH;
    if (data[0] != 1) return PHONE_ALERT_PARSE_VERSION;

    uint8_t flags = data[1];
    uint8_t category = data[2];
    size_t app_len = data[3];
    size_t sender_len = data[4];
    uint32_t event_id = get32(data + 5);
    if (event_id == 0) return PHONE_ALERT_PARSE_EVENT_ID;
    if (flags & ~(PHONE_ALERT_FLAG_INTERRUPT_FOCUS | PHONE_ALERT_FLAG_PASS_DND))
        return PHONE_ALERT_PARSE_FLAGS;
    if (category > PHONE_ALERT_ALARM) return PHONE_ALERT_PARSE_CATEGORY;
    if (app_len == 0 || app_len > APP_MAX || sender_len == 0 || sender_len > SENDER_MAX)
        return PHONE_ALERT_PARSE_LENGTH;
    if (len != HEADER_LEN + app_len + sender_len) return PHONE_ALERT_PARSE_LENGTH;
    if (!valid_utf8_text(data + HEADER_LEN, app_len) ||
        !valid_utf8_text(data + HEADER_LEN + app_len, sender_len))
        return PHONE_ALERT_PARSE_UTF8;

    PhoneAlert parsed = {};
    parsed.event_id = event_id;
    parsed.flags = flags;
    parsed.category = (PhoneAlertCategory)category;
    memcpy(parsed.app, data + HEADER_LEN, app_len);
    memcpy(parsed.sender, data + HEADER_LEN + app_len, sender_len);
    parsed.app[app_len] = '\0';
    parsed.sender[sender_len] = '\0';
    *out = parsed;
    return PHONE_ALERT_PARSE_OK;
}

const char* phone_alert_category_name(PhoneAlertCategory category) {
    switch (category) {
        case PHONE_ALERT_CALL: return "CALL";
        case PHONE_ALERT_MESSAGE: return "MESSAGE";
        case PHONE_ALERT_CALENDAR: return "CALENDAR";
        case PHONE_ALERT_REMINDER: return "REMINDER";
        case PHONE_ALERT_ALARM: return "ALARM";
        default: return "ALERT";
    }
}
