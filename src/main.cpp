/*
 * kid_arcade — ESP32-P4 portrait mode smoke test
 *
 * Target: Elecrow CrowPanel Advanced 7" ESP32-P4 (1024×600 MIPI DSI, EK79007)
 * Portrait: 600×1024 via software rotation
 * Touch: GT911 capacitive I2C
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_ldo_regulator.h"

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_ek79007.h"
#include "esp_lcd_touch_gt911.h"

#include "esp_lvgl_port.h"

#define TAG "kid_arcade"

// ── Board config (mirrors Elecrow config.h) ───────────────────────────────────

#define DISPLAY_WIDTH   1024
#define DISPLAY_HEIGHT  600

#define LCD_BIT_PER_PIXEL       16
#define LCD_MIPI_DSI_LANE_NUM   2
#define MIPI_DSI_PHY_PWR_LDO_CHAN       3
#define MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV 2500

#define PIN_NUM_LCD_RST     GPIO_NUM_NC
#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_31

#define TOUCH_GPIO_RST  GPIO_NUM_40
#define TOUCH_GPIO_INT  GPIO_NUM_42
#define TOUCH_I2C_SDA   GPIO_NUM_45
#define TOUCH_I2C_SCL   GPIO_NUM_46

// ── LDO power for MIPI DSI PHY ───────────────────────────────────────────────

static void enable_dsi_phy_power(void)
{
    static esp_ldo_channel_handle_t phy_pwr_chan = NULL;
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id    = MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan));
    ESP_LOGI(TAG, "MIPI DSI PHY power on (LDO%d: %dmV)",
             MIPI_DSI_PHY_PWR_LDO_CHAN, MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV);
}

// ── app_main ──────────────────────────────────────────────────────────────────

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "kid_arcade ESP32-P4 portrait test starting");

    // Backlight off during init
    gpio_set_direction(DISPLAY_BACKLIGHT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DISPLAY_BACKLIGHT_PIN, 0);

    // MIPI DSI PHY power must be on before DSI bus init
    enable_dsi_phy_power();

    // ── MIPI DSI bus ──────────────────────────────────────────────────────────
    esp_lcd_dsi_bus_handle_t dsi_bus;
    esp_lcd_dsi_bus_config_t dsi_bus_cfg = {};
    dsi_bus_cfg.bus_id             = 0;
    dsi_bus_cfg.num_data_lanes     = 2;
    dsi_bus_cfg.phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
    dsi_bus_cfg.lane_bit_rate_mbps = 900;
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&dsi_bus_cfg, &dsi_bus));

    // ── DBI panel IO (init commands) ──────────────────────────────────────────
    esp_lcd_panel_io_handle_t panel_io;
    esp_lcd_dbi_io_config_t dbi_cfg = EK79007_PANEL_IO_DBI_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &panel_io));

    // ── DPI config (1024×600, 60 Hz) — written out for IDF 6.0.1 compatibility ─
    // EK79007_1024_600_PANEL_60HZ_CONFIG macro uses IDF 5.x field names
    // (pixel_format, use_dma2d) which no longer exist in IDF 6.0.1.
    esp_lcd_dpi_panel_config_t dpi_cfg = {};
    dpi_cfg.virtual_channel    = 0;
    dpi_cfg.dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi_cfg.dpi_clock_freq_mhz = 52;
    dpi_cfg.in_color_format    = LCD_COLOR_FMT_RGB565;
    dpi_cfg.num_fbs            = 1;
    dpi_cfg.video_timing.h_size            = 1024;
    dpi_cfg.video_timing.v_size            = 600;
    dpi_cfg.video_timing.hsync_pulse_width = 10;
    dpi_cfg.video_timing.hsync_back_porch  = 160;
    dpi_cfg.video_timing.hsync_front_porch = 160;
    dpi_cfg.video_timing.vsync_pulse_width = 1;
    dpi_cfg.video_timing.vsync_back_porch  = 23;
    dpi_cfg.video_timing.vsync_front_porch = 12;

    // ── EK79007 panel ─────────────────────────────────────────────────────────
    ek79007_vendor_config_t vendor_cfg = {};
    vendor_cfg.mipi_config.dsi_bus    = dsi_bus;
    vendor_cfg.mipi_config.dpi_config = &dpi_cfg;

    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_cfg.bits_per_pixel = LCD_BIT_PER_PIXEL;
    panel_cfg.reset_gpio_num = PIN_NUM_LCD_RST;
    panel_cfg.vendor_config  = &vendor_cfg;
    esp_lcd_panel_handle_t panel;
    ESP_ERROR_CHECK(esp_lcd_new_panel_ek79007(panel_io, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    // ── LVGL port init ────────────────────────────────────────────────────────
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_stack = 8192;
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    // ── Add DSI display to LVGL ───────────────────────────────────────────────
    lvgl_port_display_cfg_t disp_cfg = {};
    disp_cfg.io_handle    = panel_io;
    disp_cfg.panel_handle = panel;
    disp_cfg.buffer_size  = DISPLAY_WIDTH * 50;
    disp_cfg.double_buffer = false;
    disp_cfg.hres          = DISPLAY_WIDTH;
    disp_cfg.vres          = DISPLAY_HEIGHT;
    disp_cfg.monochrome    = false;
    disp_cfg.color_format  = LV_COLOR_FORMAT_RGB565;
    disp_cfg.flags.buff_dma    = 1;  // internal DMA-capable SRAM (matches Elecrow reference)
    disp_cfg.flags.buff_spiram = 0;
    disp_cfg.flags.sw_rotate   = 1;  // SW rotation for portrait
    lvgl_port_display_dsi_cfg_t dsi_disp_cfg = {
        .flags = { .avoid_tearing = 0 },
    };
    lv_display_t *disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_disp_cfg);
    assert(disp);

    lvgl_port_lock(0);
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);
    lvgl_port_unlock();

    // ── GT911 touch ───────────────────────────────────────────────────────────
    i2c_master_bus_handle_t i2c_bus;
    i2c_master_bus_config_t i2c_cfg = {
        .i2c_port          = I2C_NUM_0,
        .sda_io_num        = TOUCH_I2C_SDA,
        .scl_io_num        = TOUCH_I2C_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags             = { .enable_internal_pullup = true },
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &i2c_bus));

    esp_lcd_panel_io_handle_t tp_io;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_cfg.scl_speed_hz = 400000;
    esp_err_t err = esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_cfg, &tp_io);
    if (err != ESP_OK) {
        tp_io_cfg.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_cfg, &tp_io));
    }

    esp_lcd_touch_handle_t tp;
    esp_lcd_touch_config_t tp_cfg = {};
    tp_cfg.x_max        = DISPLAY_WIDTH;
    tp_cfg.y_max        = DISPLAY_HEIGHT;
    tp_cfg.rst_gpio_num = TOUCH_GPIO_RST;
    tp_cfg.int_gpio_num = TOUCH_GPIO_INT;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &tp));

    lvgl_port_touch_cfg_t touch_cfg = {};
    touch_cfg.disp   = disp;
    touch_cfg.handle = tp;
    lvgl_port_add_touch(&touch_cfg);

    // ── Build UI ──────────────────────────────────────────────────────────────
    lvgl_port_lock(0);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A2E), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Kid Arcade");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFD700), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "Display OK!\nPortrait 600x1024");
    lv_obj_set_style_text_color(sub, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, -60);

    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 300, 100);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 60);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xE85D04), 0);
    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "Tap Me!");
    lv_obj_center(btn_lbl);

    lvgl_port_unlock();

    // Backlight on
    gpio_set_level(DISPLAY_BACKLIGHT_PIN, 1);
    ESP_LOGI(TAG, "Display up — portrait 600x1024");
}
