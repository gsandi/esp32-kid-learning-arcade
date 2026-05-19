cmake_minimum_required(VERSION 3.16.0)

# IDF_COMPONENT_MANAGER defaults to 0 in IDF 6.x so managed_components/ is not
# auto-scanned. Point directly at the Elecrow reference repo's pre-downloaded
# components so no copy/sync issues can break the build.
set(ELECROW_MC /Users/sandi/workspace/startup/7inch_9inch_10inch_P4_HMI_AI/managed_components)
list(APPEND EXTRA_COMPONENT_DIRS
    ${ELECROW_MC}/espressif__esp_lcd_ek79007
    ${ELECROW_MC}/espressif__esp_lcd_touch
    ${ELECROW_MC}/espressif__esp_lcd_touch_gt911
    ${ELECROW_MC}/espressif__esp_lvgl_port
    ${ELECROW_MC}/lvgl__lvgl
    ${CMAKE_SOURCE_DIR}/managed_components/espressif__esp_wifi_remote
    ${CMAKE_SOURCE_DIR}/managed_components/espressif__esp_hosted
    ${CMAKE_SOURCE_DIR}/managed_components/espressif__eppp_link
    ${CMAKE_SOURCE_DIR}/managed_components/espressif__wifi_remote_over_eppp
    ${CMAKE_SOURCE_DIR}/managed_components/espressif__esp_serial_slave_link
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(kid_arcade)

add_compile_options(-Wno-missing-field-initializers)
