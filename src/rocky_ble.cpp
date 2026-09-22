#include "rocky_ble.h"
#include "focus_apps.h"

#include "sdkconfig.h"
#include "esp_log.h"
#include <string.h>

#if CONFIG_BT_ENABLED

#include "freertos/FreeRTOS.h"
#include "esp_bt_main.h"
#include "esp_bluedroid_hci.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatts_api.h"
#include "esp_hosted.h"
// esp_hosted_bluedroid.h is a C header but currently lacks its own extern-C
// guard, so include it explicitly with C linkage from this C++ translation unit.
extern "C" {
#include "esp_hosted_bluedroid.h"
}

static const char* TAG = "rocky_ble";

namespace {

constexpr uint16_t APP_ID = 0x524B; // "RK"
constexpr uint8_t SVC_INSTANCE = 0;
constexpr size_t CFG_LEN = 8;
constexpr size_t STATE_LEN = 12;

enum AttrIndex {
    IDX_SVC,
    IDX_CFG_DECL,
    IDX_CFG_VAL,
    IDX_CFG_CCCD,
    IDX_STATE_DECL,
    IDX_STATE_VAL,
    IDX_STATE_CCCD,
    IDX_INFO_DECL,
    IDX_INFO_VAL,
    IDX_COUNT,
};

static const uint16_t UUID_PRIMARY_SERVICE = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t UUID_CHAR_DECLARE = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t UUID_CCCD = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint8_t PROP_CFG = ESP_GATT_CHAR_PROP_BIT_READ |
                                ESP_GATT_CHAR_PROP_BIT_WRITE |
                                ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t PROP_STATE = ESP_GATT_CHAR_PROP_BIT_READ |
                                  ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t PROP_INFO = ESP_GATT_CHAR_PROP_BIT_READ;
static uint8_t CCCD_OFF[2] = {0, 0};

// ESP-IDF stores 128-bit UUIDs least-significant byte first.
static uint8_t UUID_SERVICE[16] = {
    0x10,0x5c,0x7a,0x0e,0x9d,0x1f,0x21,0x8c,0x3c,0x4b,0x4a,0x9f,0x01,0x00,0x1d,0x6b
};
static uint8_t UUID_CONFIG[16] = {
    0x10,0x5c,0x7a,0x0e,0x9d,0x1f,0x21,0x8c,0x3c,0x4b,0x4a,0x9f,0x02,0x00,0x1d,0x6b
};
static uint8_t UUID_STATE[16] = {
    0x10,0x5c,0x7a,0x0e,0x9d,0x1f,0x21,0x8c,0x3c,0x4b,0x4a,0x9f,0x03,0x00,0x1d,0x6b
};
static uint8_t UUID_INFO[16] = {
    0x10,0x5c,0x7a,0x0e,0x9d,0x1f,0x21,0x8c,0x3c,0x4b,0x4a,0x9f,0x04,0x00,0x1d,0x6b
};

static uint8_t s_cfg[CFG_LEN] = {};
static uint8_t s_state[STATE_LEN] = {};
static uint8_t s_last_state[STATE_LEN] = {0xFF};
static uint8_t s_info[] = "0.2.0";
static uint16_t s_handles[IDX_COUNT] = {};
static esp_gatt_if_t s_gatts_if = ESP_GATT_IF_NONE;
static uint16_t s_conn_id = 0;
static bool s_connected = false;
static bool s_cfg_notify = false;
static bool s_state_notify = false;
static uint8_t s_adv_pending = 0;
static bool s_started = false;

static portMUX_TYPE s_cfg_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_cfg_pending = false;
static uint8_t s_cfg_staged[CFG_LEN] = {};

static inline void put16(uint8_t* b, uint16_t v) {
    b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8);
}
static inline void put32(uint8_t* b, uint32_t v) {
    b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24);
}
static inline uint16_t get16(const uint8_t* b) {
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

static void pack_config(void) {
    uint16_t mins[4];
    focus_ble_get_config(mins);
    for (int i = 0; i < 4; ++i) put16(s_cfg + i * 2, mins[i]);
}

static void pack_state(void) {
    uint8_t state, segment;
    uint16_t sessions;
    uint32_t remaining, total;
    focus_ble_get_state(&state, &segment, &sessions, &remaining, &total);
    s_state[0] = state;
    s_state[1] = segment;
    put16(s_state + 2, sessions);
    put32(s_state + 4, remaining);
    put32(s_state + 8, total);
}

static const esp_gatts_attr_db_t GATT_DB[IDX_COUNT] = {
    [IDX_SVC] = {{ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t*)&UUID_PRIMARY_SERVICE, ESP_GATT_PERM_READ,
         ESP_UUID_LEN_128, ESP_UUID_LEN_128, UUID_SERVICE}},

    [IDX_CFG_DECL] = {{ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t*)&UUID_CHAR_DECLARE, ESP_GATT_PERM_READ,
         sizeof(uint8_t), sizeof(PROP_CFG), (uint8_t*)&PROP_CFG}},
    [IDX_CFG_VAL] = {{ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, UUID_CONFIG, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
         CFG_LEN, CFG_LEN, s_cfg}},
    [IDX_CFG_CCCD] = {{ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t*)&UUID_CCCD, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
         sizeof(CCCD_OFF), sizeof(CCCD_OFF), CCCD_OFF}},

    [IDX_STATE_DECL] = {{ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t*)&UUID_CHAR_DECLARE, ESP_GATT_PERM_READ,
         sizeof(uint8_t), sizeof(PROP_STATE), (uint8_t*)&PROP_STATE}},
    [IDX_STATE_VAL] = {{ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, UUID_STATE, ESP_GATT_PERM_READ,
         STATE_LEN, STATE_LEN, s_state}},
    [IDX_STATE_CCCD] = {{ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t*)&UUID_CCCD, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
         sizeof(CCCD_OFF), sizeof(CCCD_OFF), CCCD_OFF}},

    [IDX_INFO_DECL] = {{ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t*)&UUID_CHAR_DECLARE, ESP_GATT_PERM_READ,
         sizeof(uint8_t), sizeof(PROP_INFO), (uint8_t*)&PROP_INFO}},
    [IDX_INFO_VAL] = {{ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, UUID_INFO, ESP_GATT_PERM_READ,
         sizeof(s_info) - 1, sizeof(s_info) - 1, s_info}},
};

static esp_ble_adv_data_t ADV_DATA = {
    .set_scan_rsp = false,
    // Keep the complete 128-bit service UUID in the primary packet so Android's
    // filtered scan finds Rocky Arcade. The human-readable name lives in scan response.
    .include_name = false,
    .include_txpower = false,
    .min_interval = 0x20,
    .max_interval = 0x40,
    .appearance = 0,
    .manufacturer_len = 0,
    .p_manufacturer_data = nullptr,
    .service_data_len = 0,
    .p_service_data = nullptr,
    .service_uuid_len = sizeof(UUID_SERVICE),
    .p_service_uuid = UUID_SERVICE,
    .flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,
};

static esp_ble_adv_data_t SCAN_DATA = {
    .set_scan_rsp = true,
    .include_name = true,
    .include_txpower = false,
    .min_interval = 0x20,
    .max_interval = 0x40,
    .appearance = 0,
    .manufacturer_len = 0,
    .p_manufacturer_data = nullptr,
    .service_data_len = 0,
    .p_service_data = nullptr,
    .service_uuid_len = 0,
    .p_service_uuid = nullptr,
    .flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,
};

static esp_ble_adv_params_t ADV_PARAMS = {
    .adv_int_min = 0x40,
    .adv_int_max = 0x80,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void maybe_start_advertising(void) {
    if (s_adv_pending == 0 && !s_connected) {
        esp_err_t err = esp_ble_gap_start_advertising(&ADV_PARAMS);
        if (err != ESP_OK) ESP_LOGE(TAG, "start advertising: %s", esp_err_to_name(err));
    }
}

static void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            s_adv_pending &= ~0x01;
            maybe_start_advertising();
            break;
        case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
            s_adv_pending &= ~0x02;
            maybe_start_advertising();
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS)
                ESP_LOGI(TAG, "advertising as Rocky Arcade");
            else
                ESP_LOGE(TAG, "advertising failed: %d", param->adv_start_cmpl.status);
            break;
        default:
            break;
    }
}

static void gatts_cb(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                     esp_ble_gatts_cb_param_t* param) {
    switch (event) {
        case ESP_GATTS_REG_EVT:
            if (param->reg.status != ESP_GATT_OK) {
                ESP_LOGE(TAG, "GATT registration failed: %d", param->reg.status);
                return;
            }
            s_gatts_if = gatts_if;
            esp_ble_gap_set_device_name("Rocky Arcade");
            s_adv_pending = 0x03;
            esp_ble_gap_config_adv_data(&ADV_DATA);
            esp_ble_gap_config_adv_data(&SCAN_DATA);
            esp_ble_gatts_create_attr_tab(GATT_DB, gatts_if, IDX_COUNT, SVC_INSTANCE);
            break;

        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            if (param->add_attr_tab.status != ESP_GATT_OK || param->add_attr_tab.num_handle != IDX_COUNT) {
                ESP_LOGE(TAG, "attribute table failed: status=%d handles=%d",
                         param->add_attr_tab.status, param->add_attr_tab.num_handle);
                return;
            }
            memcpy(s_handles, param->add_attr_tab.handles, sizeof(s_handles));
            esp_ble_gatts_start_service(s_handles[IDX_SVC]);
            break;

        case ESP_GATTS_CONNECT_EVT:
            s_connected = true;
            s_conn_id = param->connect.conn_id;
            ESP_LOGI(TAG, "phone connected");
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            s_connected = false;
            s_cfg_notify = s_state_notify = false;
            ESP_LOGI(TAG, "phone disconnected; advertising again");
            esp_ble_gap_start_advertising(&ADV_PARAMS);
            break;

        case ESP_GATTS_WRITE_EVT:
            if (param->write.is_prep) break;
            if (param->write.handle == s_handles[IDX_CFG_VAL] && param->write.len == CFG_LEN) {
                portENTER_CRITICAL(&s_cfg_mux);
                memcpy(s_cfg_staged, param->write.value, CFG_LEN);
                s_cfg_pending = true;
                portEXIT_CRITICAL(&s_cfg_mux);
            } else if (param->write.len == 2) {
                bool enabled = get16(param->write.value) == 1;
                if (param->write.handle == s_handles[IDX_CFG_CCCD]) s_cfg_notify = enabled;
                if (param->write.handle == s_handles[IDX_STATE_CCCD]) s_state_notify = enabled;
            }
            break;

        default:
            break;
    }
}

} // namespace

void rocky_ble_init(void) {
    if (s_started) return;
    pack_config();
    pack_state();

    esp_err_t err = esp_hosted_bt_controller_init();
    if (err != ESP_OK) { ESP_LOGE(TAG, "controller init: %s", esp_err_to_name(err)); return; }
    err = esp_hosted_bt_controller_enable();
    if (err != ESP_OK) { ESP_LOGE(TAG, "controller enable: %s", esp_err_to_name(err)); return; }

    hosted_hci_bluedroid_open();
    esp_bluedroid_hci_driver_operations_t operations = {
        .send = hosted_hci_bluedroid_send,
        .check_send_available = hosted_hci_bluedroid_check_send_available,
        .register_host_callback = hosted_hci_bluedroid_register_host_callback,
    };
    ESP_ERROR_CHECK(esp_bluedroid_attach_hci_driver(&operations));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_cb));
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_cb));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(APP_ID));
    esp_ble_gatt_set_local_mtu(185);
    s_started = true;
    ESP_LOGI(TAG, "Rocky companion service starting through ESP32-C6");
}

void rocky_ble_poll(void) {
    if (!s_started) return;

    uint8_t staged[CFG_LEN];
    bool apply = false;
    portENTER_CRITICAL(&s_cfg_mux);
    if (s_cfg_pending) {
        memcpy(staged, s_cfg_staged, CFG_LEN);
        s_cfg_pending = false;
        apply = true;
    }
    portEXIT_CRITICAL(&s_cfg_mux);

    if (apply) {
        uint16_t mins[4];
        for (int i = 0; i < 4; ++i) mins[i] = get16(staged + i * 2);
        if (focus_ble_set_config(mins)) {
            pack_config();
            esp_ble_gatts_set_attr_value(s_handles[IDX_CFG_VAL], CFG_LEN, s_cfg);
            if (s_connected && s_cfg_notify)
                esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id, s_handles[IDX_CFG_VAL],
                                            CFG_LEN, s_cfg, false);
            ESP_LOGI(TAG, "durations updated: %u/%u/%u/%u", mins[0], mins[1], mins[2], mins[3]);
        } else {
            ESP_LOGW(TAG, "rejected invalid duration write");
            pack_config();
            esp_ble_gatts_set_attr_value(s_handles[IDX_CFG_VAL], CFG_LEN, s_cfg);
        }
    }

    pack_state();
    if (memcmp(s_state, s_last_state, STATE_LEN) != 0) {
        memcpy(s_last_state, s_state, STATE_LEN);
        esp_ble_gatts_set_attr_value(s_handles[IDX_STATE_VAL], STATE_LEN, s_state);
        if (s_connected && s_state_notify)
            esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id, s_handles[IDX_STATE_VAL],
                                        STATE_LEN, s_state, false);
    }
}

#else

void rocky_ble_init(void) {
    ESP_LOGW("rocky_ble", "Bluetooth support is disabled in sdkconfig");
}

void rocky_ble_poll(void) {}

#endif
