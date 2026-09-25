// Rocky BLE compatibility service carried by the ESP32-C6 over ESP-Hosted.
#pragma once

enum RockyBleState {
    ROCKY_BLE_DISABLED = 0,
    ROCKY_BLE_STARTING,
    ROCKY_BLE_READY,
    ROCKY_BLE_CONNECTED, // Physical link established; encryption still pending.
    ROCKY_BLE_SECURE,    // Bonded and encrypted phone link.
};

// Starts the GATT server used by the existing org.saga.rocky Android app.
// Call after ESP-Hosted/Wi-Fi has connected to the C6 controller.
void rocky_ble_init(void);

// Applies staged writes and publishes changed focus state. Called from the LVGL
// service timer so BLE callbacks never touch UI-owned state directly.
void rocky_ble_poll(void);

// Read-only connection state for Settings/Admin status indicators.
RockyBleState rocky_ble_state(void);
const char* rocky_ble_device_name(void);
