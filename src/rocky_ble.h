// Rocky BLE compatibility service carried by the ESP32-C6 over ESP-Hosted.
#pragma once

// Starts the GATT server used by the existing org.saga.rocky Android app.
// Call after ESP-Hosted/Wi-Fi has connected to the C6 controller.
void rocky_ble_init(void);

// Applies staged writes and publishes changed focus state. Called from the LVGL
// service timer so BLE callbacks never touch UI-owned state directly.
void rocky_ble_poll(void);

