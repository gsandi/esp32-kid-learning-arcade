#include "voice_notes.h"

#include "driver/i2s_pdm.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "nvs.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char* TAG = "voice_notes";

namespace {

constexpr gpio_num_t MIC_CLK = GPIO_NUM_24;
constexpr gpio_num_t MIC_DATA = GPIO_NUM_26;
constexpr uint32_t SAMPLE_RATE = 16000;
constexpr uint32_t MAX_RECORD_SECONDS = 5 * 60;
constexpr char NOTES_DIR[] = "/sdcard/notes";
constexpr char PHONE_LEDGER[] = "vnphone";
constexpr char BOUNDARY[] = "----rockyArcadeVoiceNote";
constexpr size_t MAX_SYNC_FILES = 64;

i2s_chan_handle_t s_rx = nullptr;
bool s_available = false;
volatile bool s_recording = false;
volatile bool s_stop_requested = false;
volatile bool s_save_pending = false;
volatile bool s_save_ok = false;
volatile uint32_t s_saved_seconds = 0;
volatile uint32_t s_started_tick = 0;
TaskHandle_t s_record_task = nullptr;
char s_active_path[128] = {};

volatile uint16_t s_total = 0;
volatile uint16_t s_pending = 0;
volatile bool s_sync_requested = false;
volatile bool s_sync_busy = false;
volatile uint16_t s_sync_planned = 0;
volatile uint16_t s_sync_completed = 0;
volatile uint16_t s_sync_delivered = 0;
char s_sync_host[96] = {};
char s_sync_token[33] = {};
portMUX_TYPE s_sync_mux = portMUX_INITIALIZER_UNLOCKED;

lv_obj_t* s_screen = nullptr;
lv_obj_t* s_record_button = nullptr;
lv_obj_t* s_record_button_label = nullptr;
lv_obj_t* s_duration_label = nullptr;
lv_obj_t* s_status_label = nullptr;
lv_obj_t* s_count_label = nullptr;
lv_event_cb_t s_home_cb = nullptr;
uint32_t s_message_until = 0;
char s_message[64] = {};

char s_sync_files[MAX_SYNC_FILES][64] = {};

void put_le16(FILE* file, uint16_t value) {
    uint8_t b[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    fwrite(b, 1, sizeof(b), file);
}

void put_le32(FILE* file, uint32_t value) {
    uint8_t b[4] = {(uint8_t)value, (uint8_t)(value >> 8),
                    (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    fwrite(b, 1, sizeof(b), file);
}

void write_wav_header(FILE* file, uint32_t data_bytes) {
    rewind(file);
    fwrite("RIFF", 1, 4, file);
    put_le32(file, 36 + data_bytes);
    fwrite("WAVEfmt ", 1, 8, file);
    put_le32(file, 16);
    put_le16(file, 1);
    put_le16(file, 1);
    put_le32(file, SAMPLE_RATE);
    put_le32(file, SAMPLE_RATE * 2);
    put_le16(file, 2);
    put_le16(file, 16);
    fwrite("data", 1, 4, file);
    put_le32(file, data_bytes);
}

uint32_t fnv1a(const char* text) {
    uint32_t hash = 2166136261u;
    while (*text) {
        hash ^= (uint8_t)*text++;
        hash *= 16777619u;
    }
    return hash;
}

void ledger_key(const char* filename, char out[12]) {
    snprintf(out, 12, "p%08lx", (unsigned long)fnv1a(filename));
}

bool is_wav_name(const char* name) {
    size_t len = name ? strlen(name) : 0;
    return len > 4 && strcmp(name + len - 4, ".wav") == 0;
}

void refresh_counts(void) {
    if (!s_available || s_recording) return;
    DIR* dir = opendir(NOTES_DIR);
    if (!dir) return;
    nvs_handle_t ledger = 0;
    bool have_ledger = nvs_open(PHONE_LEDGER, NVS_READONLY, &ledger) == ESP_OK;
    uint16_t total = 0;
    uint16_t pending = 0;
    for (dirent* ent = readdir(dir); ent; ent = readdir(dir)) {
        if (!is_wav_name(ent->d_name)) continue;
        ++total;
        char key[12];
        ledger_key(ent->d_name, key);
        uint8_t sent = 0;
        if (!have_ledger || nvs_get_u8(ledger, key, &sent) != ESP_OK || !sent) ++pending;
    }
    if (have_ledger) nvs_close(ledger);
    closedir(dir);
    s_total = total;
    s_pending = pending;
}

void build_filename(char* out, size_t out_size) {
    time_t now = time(nullptr);
    if (now >= 1577836800) {
        struct tm local = {};
        localtime_r(&now, &local);
        strftime(out, out_size, "/sdcard/notes/note_%Y%m%d_%H%M%S.wav", &local);
    } else {
        snprintf(out, out_size, "/sdcard/notes/note_boot_%010lu.wav",
                 (unsigned long)lv_tick_get());
    }
    if (access(out, F_OK) != 0) return;
    char base[112];
    snprintf(base, sizeof(base), "%s", out);
    char* ext = strrchr(base, '.');
    if (ext) *ext = '\0';
    for (unsigned suffix = 1; suffix < 100; ++suffix) {
        snprintf(out, out_size, "%s_%u.wav", base, suffix);
        if (access(out, F_OK) != 0) return;
    }
}

int16_t amplify_sample(int16_t sample) {
    int32_t value = (int32_t)sample * 8; // Elecrow reference uses 10x; 8x leaves headroom.
    if (value > 32767) value = 32767;
    if (value < -32768) value = -32768;
    return (int16_t)value;
}

void record_task(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        FILE* file = fopen(s_active_path, "wb+");
        if (!file) {
            ESP_LOGE(TAG, "open %s failed: errno=%d", s_active_path, errno);
            s_save_ok = false;
            s_saved_seconds = 0;
            s_save_pending = true;
            s_recording = false;
            continue;
        }
        write_wav_header(file, 0);
        static int16_t samples[512];
        uint32_t data_bytes = 0;
        int32_t peak = 0;
        int64_t deadline = esp_timer_get_time() + (int64_t)MAX_RECORD_SECONDS * 1000000;
        while (!s_stop_requested && esp_timer_get_time() < deadline) {
            size_t bytes_read = 0;
            esp_err_t err = i2s_channel_read(s_rx, samples, sizeof(samples),
                                             &bytes_read, pdMS_TO_TICKS(250));
            if (err != ESP_OK || bytes_read == 0) continue;
            size_t count = bytes_read / sizeof(int16_t);
            for (size_t i = 0; i < count; ++i) {
                samples[i] = amplify_sample(samples[i]);
                int32_t absolute = samples[i] < 0 ? -(int32_t)samples[i] : samples[i];
                if (absolute > peak) peak = absolute;
            }
            size_t written = fwrite(samples, 1, bytes_read, file);
            data_bytes += (uint32_t)written;
            if (written != bytes_read) break;
        }
        write_wav_header(file, data_bytes);
        fflush(file);
        fclose(file);
        s_saved_seconds = data_bytes / (SAMPLE_RATE * 2);
        s_save_ok = data_bytes > 0 && peak > 0;
        if (!s_save_ok) unlink(s_active_path);
        ESP_LOGI(TAG, "recording %s: %lu sec, %lu bytes, peak=%ld",
                 s_save_ok ? "saved" : "failed", (unsigned long)s_saved_seconds,
                 (unsigned long)data_bytes, (long)peak);
        s_save_pending = true;
        s_recording = false;
    }
}

void begin_recording(void) {
    if (!s_available || s_recording || s_sync_busy) return;
    build_filename(s_active_path, sizeof(s_active_path));
    s_stop_requested = false;
    s_started_tick = lv_tick_get();
    s_recording = true;
    if (s_record_task) xTaskNotifyGive(s_record_task);
}

void stop_recording(void) {
    if (s_recording) s_stop_requested = true;
}

long timestamp_from_name(const char* filename) {
    unsigned year, month, day, hour, minute, second;
    if (sscanf(filename, "note_%4u%2u%2u_%2u%2u%2u",
               &year, &month, &day, &hour, &minute, &second) != 6) return 0;
    struct tm value = {};
    value.tm_year = (int)year - 1900;
    value.tm_mon = (int)month - 1;
    value.tm_mday = (int)day;
    value.tm_hour = (int)hour;
    value.tm_min = (int)minute;
    value.tm_sec = (int)second;
    return (long)mktime(&value);
}

bool upload_file(const char* host, const char* token, const char* filename) {
    char path[160];
    snprintf(path, sizeof(path), "%s/%s", NOTES_DIR, filename);
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);
    if (file_size <= 44) { fclose(file); return false; }

    char url[160];
    snprintf(url, sizeof(url), "http://%s/notes/audio", host);
    char head[520];
    int head_len = snprintf(head, sizeof(head),
        "--%s\r\nContent-Disposition: form-data; name=\"device\"\r\n\r\n"
        "Rocky Arcade\r\n--%s\r\nContent-Disposition: form-data; name=\"timestamp\"\r\n\r\n"
        "%ld\r\n--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
        "Content-Type: audio/wav\r\n\r\n",
        BOUNDARY, BOUNDARY, timestamp_from_name(filename), BOUNDARY, filename);
    char tail[64];
    int tail_len = snprintf(tail, sizeof(tail), "\r\n--%s--\r\n", BOUNDARY);
    if (head_len <= 0 || head_len >= (int)sizeof(head)) { fclose(file); return false; }

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = 60000;
    cfg.buffer_size = 2048;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { fclose(file); return false; }
    char content_type[96];
    snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", BOUNDARY);
    esp_http_client_set_header(client, "Content-Type", content_type);
    if (token && strlen(token) == 32) esp_http_client_set_header(client, "X-Rocky-Token", token);

    bool ok = esp_http_client_open(client, head_len + file_size + tail_len) == ESP_OK;
    if (ok) ok = esp_http_client_write(client, head, head_len) == head_len;
    uint8_t buffer[2048];
    while (ok) {
        size_t count = fread(buffer, 1, sizeof(buffer), file);
        if (count == 0) break;
        ok = esp_http_client_write(client, (const char*)buffer, count) == (int)count;
    }
    if (ok) ok = esp_http_client_write(client, tail, tail_len) == tail_len;
    if (ok) {
        esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        ok = status >= 200 && status < 300;
        if (!ok) ESP_LOGW(TAG, "upload %s returned HTTP %d", filename, status);
    }
    fclose(file);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

void sync_task(void*) {
    char host[sizeof(s_sync_host)];
    char token[sizeof(s_sync_token)];
    portENTER_CRITICAL(&s_sync_mux);
    snprintf(host, sizeof(host), "%s", s_sync_host);
    snprintf(token, sizeof(token), "%s", s_sync_token);
    s_sync_requested = false;
    portEXIT_CRITICAL(&s_sync_mux);
    s_sync_busy = true;
    uint16_t planned = 0;
    uint16_t completed = 0;
    uint16_t delivered_count = 0;
    uint16_t pending_count = s_pending;
    s_sync_planned = 0;
    s_sync_completed = 0;
    s_sync_delivered = 0;

    DIR* dir = opendir(NOTES_DIR);
    size_t count = 0;
    if (dir) {
        for (dirent* ent = readdir(dir); ent && count < MAX_SYNC_FILES; ent = readdir(dir)) {
            if (!is_wav_name(ent->d_name)) continue;
            size_t name_len = strnlen(ent->d_name, sizeof(s_sync_files[0]) - 1);
            memcpy(s_sync_files[count], ent->d_name, name_len);
            s_sync_files[count][name_len] = '\0';
            ++count;
        }
        closedir(dir);
    }
    for (size_t i = 0; i + 1 < count; ++i) {
        for (size_t j = i + 1; j < count; ++j) {
            if (strcmp(s_sync_files[j], s_sync_files[i]) > 0) {
                char swap[sizeof(s_sync_files[0])];
                memcpy(swap, s_sync_files[i], sizeof(swap));
                memcpy(s_sync_files[i], s_sync_files[j], sizeof(swap));
                memcpy(s_sync_files[j], swap, sizeof(swap));
            }
        }
    }

    nvs_handle_t ledger = 0;
    if (nvs_open(PHONE_LEDGER, NVS_READWRITE, &ledger) == ESP_OK) {
        for (size_t i = 0; i < count; ++i) {
            char key[12];
            ledger_key(s_sync_files[i], key);
            uint8_t sent = 0;
            if (nvs_get_u8(ledger, key, &sent) != ESP_OK || !sent) ++planned;
        }
        s_sync_planned = planned;
        for (size_t i = 0; i < count; ++i) {
            char key[12];
            ledger_key(s_sync_files[i], key);
            uint8_t sent = 0;
            if (nvs_get_u8(ledger, key, &sent) == ESP_OK && sent) continue;
            bool delivered = upload_file(host, token, s_sync_files[i]);
            ++completed;
            s_sync_completed = completed;
            if (delivered) {
                nvs_set_u8(ledger, key, 1);
                nvs_commit(ledger);
                ++delivered_count;
                s_sync_delivered = delivered_count;
                if (pending_count) --pending_count;
                s_pending = pending_count;
            }
        }
        nvs_close(ledger);
    }
    s_sync_busy = false;
    ESP_LOGI(TAG, "phone sync finished: %u/%u delivered",
             s_sync_delivered, s_sync_planned);
    vTaskDelete(nullptr);
}

lv_obj_t* make_label(lv_obj_t* parent, const char* text, const lv_font_t* font,
                     uint32_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

void record_button_cb(lv_event_t*) {
    if (s_recording) stop_recording();
    else begin_recording();
}

void back_cb(lv_event_t* event) {
    stop_recording();
    if (s_home_cb) s_home_cb(event);
}

void screen_deleted(lv_event_t*) {
    stop_recording();
    s_screen = s_record_button = s_record_button_label = nullptr;
    s_duration_label = s_status_label = s_count_label = nullptr;
}

void refresh_ui(void) {
    if (!s_screen || lv_screen_active() != s_screen) return;
    char duration[16];
    uint32_t elapsed = s_recording ? (lv_tick_get() - s_started_tick) / 1000 : 0;
    snprintf(duration, sizeof(duration), "%lu:%02lu",
             (unsigned long)(elapsed / 60), (unsigned long)(elapsed % 60));
    lv_label_set_text(s_duration_label, duration);

    if (!s_available) {
        lv_label_set_text(s_status_label, "Microphone or SD card unavailable");
        lv_label_set_text(s_record_button_label, "Unavailable");
        lv_obj_set_style_bg_color(s_record_button, lv_color_hex(0x4A4068), 0);
        lv_obj_clear_flag(s_record_button, LV_OBJ_FLAG_CLICKABLE);
    } else if (s_recording) {
        lv_label_set_text(s_status_label, "Recording - tap when finished");
        lv_label_set_text(s_record_button_label, "STOP");
        lv_obj_set_style_bg_color(s_record_button, lv_color_hex(0xD83A52), 0);
    } else {
        const char* status = "Ready for a new note";
        if (s_message[0] && lv_tick_get() < s_message_until) status = s_message;
        lv_label_set_text(s_status_label, status);
        lv_label_set_text(s_record_button_label, "RECORD");
        lv_obj_set_style_bg_color(s_record_button, lv_color_hex(0xE34A5F), 0);
    }
    char counts[80];
    snprintf(counts, sizeof(counts), "%u saved  |  %u waiting for phone",
             (unsigned)s_total, (unsigned)s_pending);
    lv_label_set_text(s_count_label, counts);
}

} // namespace

void voice_notes_init(bool sd_available) {
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = 6;
    channel.dma_frame_num = 256;
    esp_err_t err = i2s_new_channel(&channel, nullptr, &s_rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PDM channel: %s", esp_err_to_name(err));
        return;
    }
    i2s_pdm_rx_config_t config = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                   I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = MIC_CLK,
            .din = MIC_DATA,
            .invert_flags = {.clk_inv = false},
        },
    };
    config.clk_cfg.dn_sample_mode = I2S_PDM_DSR_8S;
    config.clk_cfg.bclk_div = 8;
    config.slot_cfg.slot_mask = I2S_PDM_SLOT_LEFT;
    config.slot_cfg.hp_en = true;
    config.slot_cfg.hp_cut_off_freq_hz = 35.5f;
    config.slot_cfg.amplify_num = 1;
    err = i2s_channel_init_pdm_rx_mode(s_rx, &config);
    if (err == ESP_OK) err = i2s_channel_enable(s_rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PDM init: %s", esp_err_to_name(err));
        i2s_del_channel(s_rx);
        s_rx = nullptr;
        return;
    }
    int16_t probe[256] = {};
    size_t probe_bytes = 0;
    err = i2s_channel_read(s_rx, probe, sizeof(probe), &probe_bytes, pdMS_TO_TICKS(750));
    if (err != ESP_OK || probe_bytes == 0) {
        ESP_LOGE(TAG, "PDM self-test produced no samples: %s", esp_err_to_name(err));
        i2s_channel_disable(s_rx);
        i2s_del_channel(s_rx);
        s_rx = nullptr;
        return;
    }
    int32_t probe_peak = 0;
    for (size_t i = 0; i < probe_bytes / sizeof(int16_t); ++i) {
        int32_t absolute = probe[i] < 0 ? -(int32_t)probe[i] : probe[i];
        if (absolute > probe_peak) probe_peak = absolute;
    }
    if (probe_peak == 0) ESP_LOGW(TAG, "PDM self-test returned digital silence");
    else ESP_LOGI(TAG, "PDM self-test: %u samples, raw peak=%ld",
                  (unsigned)(probe_bytes / sizeof(int16_t)), (long)probe_peak);
    if (!sd_available) {
        ESP_LOGW(TAG, "microphone is ready, but SD storage is unavailable; recorder disabled");
        i2s_channel_disable(s_rx);
        i2s_del_channel(s_rx);
        s_rx = nullptr;
        return;
    }
    if (mkdir(NOTES_DIR, 0775) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "could not create %s: errno=%d", NOTES_DIR, errno);
        i2s_channel_disable(s_rx);
        i2s_del_channel(s_rx);
        s_rx = nullptr;
        return;
    }
    s_available = true;
    refresh_counts();
    if (xTaskCreate(record_task, "voice_rec", 8192, nullptr, 5, &s_record_task) != pdPASS) {
        ESP_LOGE(TAG, "record task creation failed");
        i2s_channel_disable(s_rx);
        i2s_del_channel(s_rx);
        s_rx = nullptr;
        s_available = false;
        return;
    }
    ESP_LOGI(TAG, "PDM recorder ready on GPIO24/26; %u notes (%u pending)",
             s_total, s_pending);
}

void voice_notes_show(lv_event_cb_t home_cb) {
    s_home_cb = home_cb;
    lvgl_port_lock(0);
    lv_obj_t* screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x07070A), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    s_screen = screen;
    lv_obj_add_event_cb(screen, screen_deleted, LV_EVENT_DELETE, nullptr);

    lv_obj_t* back = lv_button_create(screen);
    lv_obj_set_size(back, 64, 54);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 18, 18);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x241440), 0);
    lv_obj_set_style_radius(back, 18, 0);
    lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* back_text = make_label(back, "<", &lv_font_montserrat_28, 0xFFFFFF);
    lv_obj_center(back_text);

    lv_obj_t* title = make_label(screen, "VOICE NOTES", &lv_font_montserrat_28, 0xF4C95D);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_t* subtitle = make_label(screen, "Say it now. Send it to your phone later.",
                                    &lv_font_montserrat_18, 0xA99CB8);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 92);

    s_duration_label = make_label(screen, "0:00", &lv_font_montserrat_48, 0xFFF7E8);
    lv_obj_align(s_duration_label, LV_ALIGN_TOP_MID, 0, 190);
    s_status_label = make_label(screen, "", &lv_font_montserrat_20, 0xB8A9CB);
    lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, 270);

    s_record_button = lv_button_create(screen);
    lv_obj_set_size(s_record_button, 310, 310);
    lv_obj_align(s_record_button, LV_ALIGN_CENTER, 0, 35);
    lv_obj_set_style_radius(s_record_button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_record_button, 10, 0);
    lv_obj_set_style_border_color(s_record_button, lv_color_hex(0xFF8797), 0);
    // A 310 px circle plus a 42 px software blur needs a ~78 KB temporary
    // corner mask in LVGL. Its allocator cannot guarantee that contiguous
    // block after the two clock canvases are active, and LV_ASSERT_MALLOC then
    // stalls the render task. The two-tone 10 px ring keeps the visual depth
    // without any transient shadow allocation.
    lv_obj_set_style_shadow_width(s_record_button, 0, 0);
    lv_obj_add_event_cb(s_record_button, record_button_cb, LV_EVENT_CLICKED, nullptr);
    s_record_button_label = make_label(s_record_button, "RECORD",
                                       &lv_font_montserrat_32, 0xFFFFFF);
    lv_obj_center(s_record_button_label);

    s_count_label = make_label(screen, "", &lv_font_montserrat_20, 0xA99CB8);
    lv_obj_align(s_count_label, LV_ALIGN_BOTTOM_MID, 0, -96);
    lv_obj_t* hint = make_label(screen, "Notes stay on the SD card until the Rocky app collects them.",
                                &lv_font_montserrat_18, 0x71677F);
    lv_obj_set_width(hint, 510);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -42);

    refresh_ui();
    lv_screen_load_anim(screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, true);
    lvgl_port_unlock();
}

void voice_notes_poll(void) {
    if (s_save_pending) {
        s_save_pending = false;
        if (s_save_ok) {
            refresh_counts();
            snprintf(s_message, sizeof(s_message), "Saved %lu-second voice note",
                     (unsigned long)s_saved_seconds);
        } else {
            snprintf(s_message, sizeof(s_message), "Recording failed - please try again");
        }
        s_message_until = lv_tick_get() + 5000;
    }
    if (s_sync_requested && !s_sync_busy && !s_recording) {
        if (xTaskCreate(sync_task, "voice_sync", 10240, nullptr, 4, nullptr) != pdPASS) {
            s_sync_requested = false;
            ESP_LOGE(TAG, "sync task creation failed");
        }
    }
    refresh_ui();
}

bool voice_notes_available(void) { return s_available; }
bool voice_notes_recording(void) { return s_recording; }

void voice_notes_counts(uint16_t* total, uint16_t* pending_phone) {
    if (total) *total = s_total;
    if (pending_phone) *pending_phone = s_pending;
}

void voice_notes_request_sync(const char* host, const char* token) {
    if (!s_available || !host || !host[0] || s_sync_busy || s_sync_requested) return;
    portENTER_CRITICAL(&s_sync_mux);
    snprintf(s_sync_host, sizeof(s_sync_host), "%s", host);
    if (token && strlen(token) == 32) snprintf(s_sync_token, sizeof(s_sync_token), "%s", token);
    else s_sync_token[0] = '\0';
    s_sync_requested = true;
    portEXIT_CRITICAL(&s_sync_mux);
    ESP_LOGI(TAG, "phone note sync requested for %s", host);
}

void voice_notes_sync_progress(bool* busy, uint16_t* planned,
                               uint16_t* completed, uint16_t* delivered) {
    if (busy) *busy = s_sync_busy || s_sync_requested;
    if (planned) *planned = s_sync_planned;
    if (completed) *completed = s_sync_completed;
    if (delivered) *delivered = s_sync_delivered;
}
