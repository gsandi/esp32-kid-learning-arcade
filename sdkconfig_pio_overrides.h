/* sdkconfig_pio_overrides.h
 * Forced-include shim: only defines values that are wholly absent from
 * sdkconfig.h when PlatformIO + IDF 6.0.x fails to load Kconfig.wifi.in.
 * Do NOT redefine anything that sdkconfig.h already provides.
 */
#pragma once

/* ── WIFI_RMT buffer macros (needed by injected esp_wifi.h WIFI_INIT_CONFIG_DEFAULT) ── */
/* These are normally generated from Kconfig.wifi.in which isn't loaded. */
#ifndef CONFIG_WIFI_RMT_STATIC_RX_BUFFER_NUM
#define CONFIG_WIFI_RMT_STATIC_RX_BUFFER_NUM 16
#endif
#ifndef CONFIG_WIFI_RMT_DYNAMIC_RX_BUFFER_NUM
#define CONFIG_WIFI_RMT_DYNAMIC_RX_BUFFER_NUM 32
#endif
#ifndef CONFIG_WIFI_RMT_TX_BUFFER_TYPE
#define CONFIG_WIFI_RMT_TX_BUFFER_TYPE 0
#endif
#ifndef CONFIG_WIFI_RMT_DYNAMIC_RX_MGMT_BUF
#define CONFIG_WIFI_RMT_DYNAMIC_RX_MGMT_BUF 0
#endif
#ifndef CONFIG_WIFI_RMT_ESPNOW_MAX_ENCRYPT_NUM
#define CONFIG_WIFI_RMT_ESPNOW_MAX_ENCRYPT_NUM 7
#endif
