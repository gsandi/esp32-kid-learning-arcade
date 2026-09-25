#include "../src/alert_protocol.h"

#include <assert.h>
#include <string.h>
#include <string>
#include <vector>

static std::vector<uint8_t> packet(uint32_t id, uint8_t category,
                                   const char* app, const char* sender) {
    size_t al = strlen(app), sl = strlen(sender);
    std::vector<uint8_t> p = {1, PHONE_ALERT_FLAG_INTERRUPT_FOCUS, category,
                              (uint8_t)al, (uint8_t)sl, (uint8_t)id,
                              (uint8_t)(id >> 8), (uint8_t)(id >> 16),
                              (uint8_t)(id >> 24)};
    p.insert(p.end(), app, app + al);
    p.insert(p.end(), sender, sender + sl);
    return p;
}

int main() {
    PhoneAlert out = {};
    auto good = packet(42, PHONE_ALERT_MESSAGE, "WhatsApp", "Maya");
    assert(phone_alert_parse(good.data(), good.size(), &out) == PHONE_ALERT_PARSE_OK);
    assert(out.event_id == 42 && out.flags == PHONE_ALERT_FLAG_INTERRUPT_FOCUS &&
           !strcmp(out.app, "WhatsApp") && !strcmp(out.sender, "Maya"));

    auto unicode = packet(43, PHONE_ALERT_CALL, "Phone", "Jos\xC3\xA9");
    assert(phone_alert_parse(unicode.data(), unicode.size(), &out) == PHONE_ALERT_PARSE_OK);

    good.push_back('x');
    assert(phone_alert_parse(good.data(), good.size(), &out) == PHONE_ALERT_PARSE_LENGTH);

    auto bad_utf8 = packet(44, PHONE_ALERT_OTHER, "App", "\xC0\xAF");
    assert(phone_alert_parse(bad_utf8.data(), bad_utf8.size(), &out) == PHONE_ALERT_PARSE_UTF8);

    auto control = packet(45, PHONE_ALERT_CALENDAR, "Calendar", "Line\nTwo");
    assert(phone_alert_parse(control.data(), control.size(), &out) == PHONE_ALERT_PARSE_UTF8);

    auto flags = packet(46, PHONE_ALERT_MESSAGE, "Messages", "Maya");
    flags[1] = 0x80;
    assert(phone_alert_parse(flags.data(), flags.size(), &out) == PHONE_ALERT_PARSE_FLAGS);

    auto zero_id = packet(0, PHONE_ALERT_REMINDER, "Tasks", "Reminder");
    assert(phone_alert_parse(zero_id.data(), zero_id.size(), &out) == PHONE_ALERT_PARSE_EVENT_ID);

    std::string app_max(24, 'A');
    std::string sender_max(63, 'S');
    auto maximum = packet(47, PHONE_ALERT_ALARM, app_max.c_str(), sender_max.c_str());
    assert(maximum.size() == 96);
    assert(phone_alert_parse(maximum.data(), maximum.size(), &out) == PHONE_ALERT_PARSE_OK);

    auto partial_codepoint = packet(48, PHONE_ALERT_OTHER, "App", "\xE2\x82");
    assert(phone_alert_parse(partial_codepoint.data(), partial_codepoint.size(), &out) ==
           PHONE_ALERT_PARSE_UTF8);
    return 0;
}
