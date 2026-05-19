# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS ${app_sources}
    REQUIRES
        esp_lcd_ek79007
        esp_lcd_touch
        esp_lcd_touch_gt911
        esp_lvgl_port
        lvgl
        esp_lcd
        esp_driver_gpio
        esp_driver_i2c
        esp_hw_support
        driver
        esp_wifi
        esp_event
        esp_netif
        lwip
        esp_http_client
)
