// Stub for IDF 6.x: esp_interface.h was removed; types moved to esp_wifi_types.h
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    ESP_IF_WIFI_STA = 0,
    ESP_IF_WIFI_AP,
    ESP_IF_WIFI_NAN,
    ESP_IF_ETH,
    ESP_IF_MAX
} esp_interface_t;
#ifdef __cplusplus
}
#endif
