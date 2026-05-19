/*
 * kid_arcade — ESP32-P4 CrowPanel 7" (1024×600 MIPI DSI, portrait 600×1024)
 * Full game: Math + Reading, app-launcher UI, LVGL 9, IDF 6.0.1
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_ldo_regulator.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_ek79007.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lvgl_port.h"

#define TAG "kid_arcade"

// ── Board config ──────────────────────────────────────────────────────────────
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

// ââ Backlight LEDC PWM ââ
#define BL_LEDC_TIMER     LEDC_TIMER_0
#define BL_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define BL_LEDC_CHANNEL   LEDC_CHANNEL_0
#define BL_LEDC_RES       LEDC_TIMER_10_BIT   // 0..1023
#define BL_LEDC_FREQ_HZ   25000
#define BL_DUTY_MAX       1023
#define BRIGHTNESS_MIN    10
#define BRIGHTNESS_DEF    75

// ── Game config ───────────────────────────────────────────────────────────────
#define QUESTIONS_PER_ROUND 5
#define ADMIN_PIN           "0000"

// ── Colors — vibrant, kid-friendly, high-contrast ─────────────────────────────
#define C_BG          0x1A0533   // deep purple background
#define C_HEADER      0x2D1B69   // mid-purple header / nav bar
#define C_MATH        0xFF6B35   // vivid orange (math card)
#define C_MATH_DK     0xC74A1E   // pressed math
#define C_READ        0x4ECDC4   // teal (reading card)
#define C_READ_DK     0x35968F   // pressed reading
#define C_CORRECT     0x06D6A0   // mint green
#define C_CORRECT_DK  0x04A87D   // pressed correct
#define C_WRONG       0xFFB347   // warm amber (soft, no red flash)
#define C_WRONG_DK    0xD9933A   // pressed wrong
#define C_GOLD        0xFFD166   // gold / stars
#define C_BTN         0x3A2A7A   // default purple-blue button
#define C_BTN_PRESS   0x55409E   // pressed button
#define C_BTN_ALT     0x6A4FB5   // accent button (numpad helpers)
#define C_DANGER      0xB5485E   // reset / destructive (muted rose, not harsh red)
#define C_DANGER_DK   0x8F3849
#define C_CARD_TXT    0xFFFFFF   // white text
#define C_SUBTEXT     0xBBAADD   // soft lavender subtext
#define C_STAR        0xFFD166   // star gold (alias)

// Logical display size after sw_rotate + ROTATION_270 (portrait)
#define SCR_W  600
#define SCR_H  1024

// ── Question bank structs ─────────────────────────────────────────────────────
enum QType {
    QT_COUNT, QT_MISSING_NUM, QT_TEN_FRAME, QT_ADD,
    QT_MAKE10, QT_STARTS_WITH, QT_MISSING_LETTER,
    QT_RHYME, QT_UPPER_LOWER, QT_SKIP, QT_MULTIPLY,
    QT__COUNT
};

struct CountQ   { const char* prompt; int count; int opts[3]; int correct; };
struct MissNumQ { int seq[4]; int opts[3]; int correct; };
struct AddQ     { int left; int right; int opts[3]; int correct; };
struct Make10Q  { int left; int opts[3]; int correct; };
struct TenFrameQ{ int filled; int opts[3]; int correct; };
struct StartsQ  { char letter; const char* opts[3]; int correct; };
struct MissLetQ { const char* shown; char opts[3]; int correct; };
struct RhymeQ   { const char* target; const char* opts[3]; int correct; };
struct UpperLowQ{ char shown; char opts[3]; int correct; };
struct SkipQ    { int step; int shown[3]; int opts[3]; int correct; };
struct MultiplyQ{ int left; int right; int opts[3]; int correct; };

// ── Question banks ────────────────────────────────────────────────────────────
static const CountQ COUNT_DEF[] = {
    {"How many apples?",    3,  {2,3,4},  3},
    {"How many apples?",    6,  {5,6,7},  6},
    {"How many balls?",     2,  {1,2,3},  2},
    {"How many balls?",     5,  {4,5,6},  5},
    {"How many balls?",     9,  {7,8,9},  9},
    {"How many stars?",     4,  {3,4,5},  4},
    {"How many stars?",     7,  {6,7,8},  7},
    {"How many flowers?",   1,  {1,2,3},  1},
    {"How many flowers?",   8,  {6,8,9},  8},
    {"How many moons?",     3,  {2,3,4},  3},
    {"How many hearts?",    4,  {3,4,5},  4},
    {"How many hearts?",    9,  {7,8,9},  9},
    {"How many triangles?", 2,  {2,3,4},  2},
    {"How many triangles?", 5,  {4,5,6},  5},
    {"How many squares?",   3,  {2,3,4},  3},
    {"How many squares?",   6,  {5,6,7},  6},
};
static const int COUNT_N = sizeof(COUNT_DEF)/sizeof(COUNT_DEF[0]);

static const MissNumQ MISSNUM_DEF[] = {
    {{ 3, 4,-1, 6},{2, 5, 7}, 5},
    {{ 7, 8, 9,-1},{7,10,11},10},
    {{-1, 2, 3, 4},{1, 5, 0}, 1},
    {{ 5,-1, 7, 8},{4, 6, 9}, 6},
    {{ 1, 2,-1, 4},{3, 5, 6}, 3},
    {{ 6, 7, 8,-1},{9,10, 5}, 9},
    {{-1, 9,10,11},{7, 8,12}, 8},
    {{12,13,-1,15},{11,14,16},14},
    {{17,18,19,-1},{18,20,21},20},
    {{-1,16,17,18},{14,15,19},15},
    {{10,-1,12,13},{11,14, 9},11},
};
static const int MISSNUM_N = sizeof(MISSNUM_DEF)/sizeof(MISSNUM_DEF[0]);

static const AddQ ADD_DEF[] = {
    {2,3,{4,5,6},  5}, {4,5,{8,9,10}, 9}, {1,6,{6,7,8},  7},
    {3,4,{6,7,8},  7}, {2,7,{8,9,10}, 9}, {1,1,{1,2,3},  2},
    {5,4,{7,8,9},  9}, {6,2,{7,8,9},  8}, {3,5,{7,8,9},  8},
    {2,6,{7,8,9},  8}, {4,3,{5,6,7},  7}, {1,4,{4,5,6},  5},
};
static const int ADD_N = sizeof(ADD_DEF)/sizeof(ADD_DEF[0]);

static const Make10Q MAKE10_DEF[] = {
    {6,{3,4,5},4}, {3,{6,7,8},7}, {7,{2,3,4},3}, {1,{7,8,9},9},
    {4,{5,6,7},6}, {8,{1,2,3},2}, {2,{7,8,9},8}, {5,{4,5,6},5},
    {9,{0,1,2},1}, {0,{8,9,10},10},
};
static const int MAKE10_N = sizeof(MAKE10_DEF)/sizeof(MAKE10_DEF[0]);

static const TenFrameQ TENFRAME_DEF[] = {
    {3,{2,3,4},3}, {6,{5,6,7},6}, {8,{7,8,9},8}, {4,{3,4,5},4},
    {10,{8,9,10},10}, {1,{1,2,3},1}, {5,{4,5,6},5}, {7,{6,7,8},7},
};
static const int TENFRAME_N = sizeof(TENFRAME_DEF)/sizeof(TENFRAME_DEF[0]);

static const StartsQ STARTS_DEF[] = {
    {'A',{"Cat","Apple","Sun"},1},  {'B',{"Cat","Ball","Sun"},1},
    {'C',{"Cup","Pen","Hat"},0},    {'D',{"Apple","Duck","Car"},1},
    {'E',{"Egg","Cat","Sun"},0},    {'F',{"Cat","Pen","Fish"},2},
    {'G',{"Gum","Cat","Pen"},0},    {'H',{"Cat","Pen","Hat"},2},
    {'I',{"Ice","Cat","Sun"},0},    {'J',{"Cat","Jam","Pen"},1},
    {'K',{"Cat","Pen","Kite"},2},   {'L',{"Pen","Cat","Lion"},2},
    {'M',{"Moon","Dog","Fish"},0},  {'N',{"Cat","Net","Pen"},1},
    {'P',{"Pen","Cat","Sun"},0},    {'R',{"Cat","Pen","Rain"},2},
    {'S',{"Tree","Star","Book"},1}, {'T',{"Cat","Tree","Pen"},1},
};
static const int STARTS_N = sizeof(STARTS_DEF)/sizeof(STARTS_DEF[0]);

static const MissLetQ MISSLET_DEF[] = {
    {"C_T",{'A','O','E'},0}, {"D_G",{'A','O','U'},1},
    {"S_N",{'U','I','A'},0}, {"H_T",{'A','O','I'},0},
    {"B_T",{'I','A','U'},1}, {"P_N",{'E','O','I'},0},
    {"C_P",{'U','A','O'},0}, {"M_P",{'A','I','O'},0},
    {"F_X",{'O','A','I'},0}, {"L_G",{'O','E','A'},0},
};
static const int MISSLET_N = sizeof(MISSLET_DEF)/sizeof(MISSLET_DEF[0]);

static const RhymeQ RHYME_DEF[] = {
    {"cat", {"hat","dog","sun"},0},  {"sun", {"cat","fun","ball"},1},
    {"bug", {"hat","rug","pen"},1},  {"hop", {"hat","dog","top"},2},
    {"cake",{"lake","duck","sun"},0},{"red", {"bed","pen","sun"},0},
    {"pig", {"cat","big","sun"},1},  {"fish",{"car","dish","sun"},1},
    {"car", {"jar","pen","fish"},0}, {"sing",{"sun","ring","cat"},1},
};
static const int RHYME_N = sizeof(RHYME_DEF)/sizeof(RHYME_DEF[0]);

static const UpperLowQ UPPER_DEF[] = {
    {'b',{'B','D','P'},0}, {'d',{'B','D','Q'},1}, {'p',{'B','P','Q'},1},
    {'q',{'P','D','Q'},2}, {'a',{'A','O','U'},0}, {'e',{'F','E','B'},1},
    {'g',{'C','G','Q'},1}, {'m',{'N','W','M'},2}, {'n',{'M','N','H'},1},
    {'r',{'P','B','R'},2}, {'s',{'Z','S','C'},1}, {'t',{'F','T','L'},1},
};
static const int UPPER_N = sizeof(UPPER_DEF)/sizeof(UPPER_DEF[0]);

static const SkipQ SKIP_DEF[] = {
    {2,{2,4,6},{7,8,9},8},    {2,{4,6,8},{9,10,11},10},
    {2,{6,8,10},{11,12,13},12},{2,{8,10,12},{13,14,15},14},
    {2,{0,2,4},{5,6,7},6},    {2,{10,12,14},{15,16,17},16},
    {3,{3,6,9},{10,11,12},12}, {3,{6,9,12},{13,14,15},15},
    {3,{9,12,15},{16,17,18},18},{3,{0,3,6},{7,8,9},9},
    {3,{12,15,18},{19,20,21},21},{3,{15,18,21},{22,23,24},24},
    {4,{4,8,12},{13,16,15},16}, {4,{8,12,16},{17,20,19},20},
    {4,{12,16,20},{21,24,25},24},{4,{0,4,8},{9,11,12},12},
    {5,{5,10,15},{18,19,20},20},{5,{10,15,20},{23,24,25},25},
    {5,{15,20,25},{28,29,30},30},{5,{0,5,10},{13,14,15},15},
};
static const int SKIP_N = sizeof(SKIP_DEF)/sizeof(SKIP_DEF[0]);

static const MultiplyQ MULT_DEF[] = {
    {2,1,{4,2,3},2},   {2,2,{2,4,6},4},   {2,3,{4,6,8},6},
    {2,4,{6,8,10},8},  {2,5,{8,10,12},10}, {2,6,{10,12,14},12},
    {2,7,{12,14,16},14},{2,8,{14,16,18},16},{2,9,{16,18,20},18},
    {2,10,{18,20,22},20},{3,1,{6,3,9},3},  {3,2,{3,6,9},6},
    {3,3,{6,9,12},9},  {3,4,{9,12,15},12}, {3,5,{12,15,18},15},
    {3,6,{15,18,21},18},{3,7,{18,21,24},21},{3,8,{21,24,27},24},
    {3,9,{24,27,30},27},{3,10,{27,30,33},30},{4,1,{8,4,3},4},
    {4,2,{4,8,12},8},  {4,3,{8,12,16},12}, {4,4,{12,16,20},16},
    {4,5,{16,20,24},20},{5,1,{5,10,3},5},  {5,2,{5,10,15},10},
    {5,3,{10,15,20},15},{5,4,{15,20,25},20},{5,5,{20,25,30},25},
    {5,6,{25,30,35},30},{5,7,{30,35,40},35},{5,8,{35,40,45},40},
    {5,9,{40,45,50},45},{5,10,{45,50,55},50},
};
static const int MULT_N = sizeof(MULT_DEF)/sizeof(MULT_DEF[0]);

// Math types used per game mode
static const QType MATH_TYPES[] = {
    QT_COUNT, QT_MISSING_NUM, QT_TEN_FRAME, QT_ADD, QT_MAKE10, QT_SKIP, QT_MULTIPLY
};
static const QType READ_TYPES[] = {
    QT_STARTS_WITH, QT_MISSING_LETTER, QT_RHYME, QT_UPPER_LOWER
};

// ── Game state ────────────────────────────────────────────────────────────────
static int32_t g_stars         = 0;
static int     g_game          = 0;  // 0=math 1=reading
static int     g_q_idx         = 0;  // 0..4
static int     g_stars_round   = 0;
static bool    g_last_correct  = false;

struct RoundEntry { QType type; int idx; };
static RoundEntry g_round[QUESTIONS_PER_ROUND];

// Prepared display for current question
struct QDisplay {
    char prompt[220];
    char extra[80];      // sequence line (skip count, ten-frame label, etc.)
    char opts[3][40];
    int  correct_idx;    // 0..2
    int  vis_type;       // 0=none 1=dots 2=tenframe
    int  vis_count;
};
static QDisplay g_qd;

// PIN state
static char g_pin_buf[5] = "";
static int  g_pin_len    = 0;

// ── NVS helpers ───────────────────────────────────────────────────────────────
static void nvs_load_stars(void) {
    nvs_handle_t h;
    if (nvs_open("learning", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i32(h, "stars", &g_stars);
        nvs_close(h);
    }
}
static void nvs_save_stars(void) {
    nvs_handle_t h;
    if (nvs_open("learning", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, "stars", g_stars);
        nvs_commit(h);
        nvs_close(h);
    }
}

// Brightness (LEDC PWM backlight)
static uint8_t g_brightness = BRIGHTNESS_DEF;  // 0-100 (clamped >= BRIGHTNESS_MIN)

static void set_brightness(uint8_t pct) {
    if (pct > 100) pct = 100;
    if (pct < BRIGHTNESS_MIN) pct = BRIGHTNESS_MIN;
    g_brightness = pct;
    uint32_t duty = ((uint32_t)BL_DUTY_MAX * pct) / 100;
    ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty);
    ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL);
}

static void brightness_init(void) {
    ledc_timer_config_t tcfg = {};
    tcfg.speed_mode      = BL_LEDC_MODE;
    tcfg.duty_resolution = BL_LEDC_RES;
    tcfg.timer_num       = BL_LEDC_TIMER;
    tcfg.freq_hz         = BL_LEDC_FREQ_HZ;
    tcfg.clk_cfg         = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&tcfg));

    ledc_channel_config_t ccfg = {};
    ccfg.gpio_num   = DISPLAY_BACKLIGHT_PIN;
    ccfg.speed_mode = BL_LEDC_MODE;
    ccfg.channel    = BL_LEDC_CHANNEL;
    ccfg.timer_sel  = BL_LEDC_TIMER;
    ccfg.intr_type  = LEDC_INTR_DISABLE;
    ccfg.duty       = 0;   // start dark; ramp up after display init
    ccfg.hpoint     = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&ccfg));
}

static void brightness_save(void) {
    nvs_handle_t h;
    if (nvs_open("learning", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "brightness", g_brightness);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void brightness_load(void) {
    uint8_t v = BRIGHTNESS_DEF;
    nvs_handle_t h;
    if (nvs_open("learning", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "brightness", &v);   // leaves v unchanged if key absent
        nvs_close(h);
    }
    if (v > 100) v = 100;
    if (v < BRIGHTNESS_MIN) v = BRIGHTNESS_MIN;
    g_brightness = v;
}

// ── Random helper ─────────────────────────────────────────────────────────────
static int rnd(int max) {
    if (max <= 0) return 0;
    return (int)(esp_random() % (uint32_t)max);
}

// ── Round builder ─────────────────────────────────────────────────────────────
static int bank_size(QType t) {
    switch (t) {
        case QT_COUNT:        return COUNT_N;
        case QT_MISSING_NUM:  return MISSNUM_N;
        case QT_TEN_FRAME:    return TENFRAME_N;
        case QT_ADD:          return ADD_N;
        case QT_MAKE10:       return MAKE10_N;
        case QT_STARTS_WITH:  return STARTS_N;
        case QT_MISSING_LETTER:return MISSLET_N;
        case QT_RHYME:        return RHYME_N;
        case QT_UPPER_LOWER:  return UPPER_N;
        case QT_SKIP:         return SKIP_N;
        case QT_MULTIPLY:     return MULT_N;
        default:              return 1;
    }
}

static void build_round(void) {
    const QType* pool = (g_game == 0) ? MATH_TYPES : READ_TYPES;
    int pool_n        = (g_game == 0)
        ? (int)(sizeof(MATH_TYPES)/sizeof(MATH_TYPES[0]))
        : (int)(sizeof(READ_TYPES)/sizeof(READ_TYPES[0]));

    for (int i = 0; i < QUESTIONS_PER_ROUND; i++) {
        g_round[i].type = pool[rnd(pool_n)];
        g_round[i].idx  = rnd(bank_size(g_round[i].type));
    }
    g_q_idx       = 0;
    g_stars_round = 0;
}

// ── Prepare display struct for current round entry ────────────────────────────
static void fill_int_opts(const int src[3], int correct) {
    g_qd.correct_idx = -1;
    for (int i = 0; i < 3; i++) {
        snprintf(g_qd.opts[i], 40, "%d", src[i]);
        if (src[i] == correct) g_qd.correct_idx = i;
    }
}

static void prepare_question(void) {
    QType t   = g_round[g_q_idx].type;
    int   idx = g_round[g_q_idx].idx;

    g_qd.extra[0]  = '\0';
    g_qd.vis_type  = 0;
    g_qd.vis_count = 0;

    switch (t) {
        case QT_COUNT: {
            const CountQ& q = COUNT_DEF[idx];
            snprintf(g_qd.prompt, sizeof(g_qd.prompt), "%s", q.prompt);
            g_qd.vis_type  = 1;
            g_qd.vis_count = q.count;
            fill_int_opts(q.opts, q.correct);
            break;
        }
        case QT_MISSING_NUM: {
            const MissNumQ& q = MISSNUM_DEF[idx];
            char seq[80] = "";
            for (int i = 0; i < 4; i++) {
                char part[12];
                if (q.seq[i] == -1) snprintf(part, sizeof(part), "%s?", i>0?", ":"");
                else                snprintf(part, sizeof(part), "%s%d", i>0?", ":"", q.seq[i]);
                strncat(seq, part, sizeof(seq)-strlen(seq)-1);
            }
            snprintf(g_qd.prompt, sizeof(g_qd.prompt), "What is the missing number?");
            snprintf(g_qd.extra, sizeof(g_qd.extra), "%s", seq);
            fill_int_opts(q.opts, q.correct);
            break;
        }
        case QT_TEN_FRAME: {
            const TenFrameQ& q = TENFRAME_DEF[idx];
            snprintf(g_qd.prompt, sizeof(g_qd.prompt), "How many are filled?");
            g_qd.vis_type  = 2;
            g_qd.vis_count = q.filled;
            fill_int_opts(q.opts, q.correct);
            break;
        }
        case QT_ADD: {
            const AddQ& q = ADD_DEF[idx];
            snprintf(g_qd.prompt, sizeof(g_qd.prompt), "What is %d + %d?", q.left, q.right);
            fill_int_opts(q.opts, q.correct);
            break;
        }
        case QT_MAKE10: {
            const Make10Q& q = MAKE10_DEF[idx];
            snprintf(g_qd.prompt, sizeof(g_qd.prompt), "%d + ? = 10", q.left);
            fill_int_opts(q.opts, q.correct);
            break;
        }
        case QT_SKIP: {
            const SkipQ& q = SKIP_DEF[idx];
            snprintf(g_qd.prompt, sizeof(g_qd.prompt), "Count by %ds. What comes next?", q.step);
            snprintf(g_qd.extra, sizeof(g_qd.extra), "%d, %d, %d, ?", q.shown[0], q.shown[1], q.shown[2]);
            fill_int_opts(q.opts, q.correct);
            break;
        }
        case QT_MULTIPLY: {
            const MultiplyQ& q = MULT_DEF[idx];
            snprintf(g_qd.prompt, sizeof(g_qd.prompt), "What is %d x %d?", q.left, q.right);
            fill_int_opts(q.opts, q.correct);
            break;
        }
        case QT_STARTS_WITH: {
            const StartsQ& q = STARTS_DEF[idx];
            snprintf(g_qd.prompt, sizeof(g_qd.prompt), "Which word starts with '%c'?", q.letter);
            for (int i = 0; i < 3; i++) snprintf(g_qd.opts[i], 40, "%s", q.opts[i]);
            g_qd.correct_idx = q.correct;
            break;
        }
        case QT_MISSING_LETTER: {
            const MissLetQ& q = MISSLET_DEF[idx];
            snprintf(g_qd.prompt, sizeof(g_qd.prompt), "Fill in the missing letter:");
            snprintf(g_qd.extra, sizeof(g_qd.extra), "%s", q.shown);
            for (int i = 0; i < 3; i++) snprintf(g_qd.opts[i], 40, "%c", q.opts[i]);
            g_qd.correct_idx = q.correct;
            break;
        }
        case QT_RHYME: {
            const RhymeQ& q = RHYME_DEF[idx];
            snprintf(g_qd.prompt, sizeof(g_qd.prompt), "Which word rhymes with \"%s\"?", q.target);
            for (int i = 0; i < 3; i++) snprintf(g_qd.opts[i], 40, "%s", q.opts[i]);
            g_qd.correct_idx = q.correct;
            break;
        }
        case QT_UPPER_LOWER: {
            const UpperLowQ& q = UPPER_DEF[idx];
            snprintf(g_qd.prompt, sizeof(g_qd.prompt), "What is the uppercase of '%c'?", q.shown);
            for (int i = 0; i < 3; i++) snprintf(g_qd.opts[i], 40, "%c", q.opts[i]);
            g_qd.correct_idx = q.correct;
            break;
        }
        default: break;
    }
}

// ── Forward declarations ──────────────────────────────────────────────────────
static void show_home(void);
static void launch_arcade(void);
static void launch_settings(void);
static void show_question(void);
static void show_feedback(void);
static void show_round_complete(void);
static void show_pin(void);
static void show_admin(void);

// ── Common style helpers ──────────────────────────────────────────────────────
static void style_screen(lv_obj_t* scr) {
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t* make_label(lv_obj_t* parent, const char* txt,
                            const lv_font_t* font, uint32_t color) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    return lbl;
}

// Transparent flex column — used instead of absolute x/y for centered stacks.
static lv_obj_t* make_col(lv_obj_t* parent, int w, int h, int gap) {
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(c, gap, 0);
    return c;
}

static lv_obj_t* make_btn(lv_obj_t* parent, const char* txt,
                           int w, int h, uint32_t bg,
                           lv_event_cb_t cb, void* ud) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(C_BTN_PRESS), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 22, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 14, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(btn, 70, 0);
    lv_obj_set_style_shadow_ofs_y(btn, 5, 0);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(C_CARD_TXT), 0);
    lv_obj_center(lbl);
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);
    return btn;
}

// Purple header bar pinned to the top. Returns the bar for extra widgets.
static lv_obj_t* make_header(lv_obj_t* scr, int h) {
    lv_obj_t* hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, SCR_W, h);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(C_HEADER), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_set_style_shadow_width(hdr, 16, 0);
    lv_obj_set_style_shadow_color(hdr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(hdr, 60, 0);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    return hdr;
}

// ── Ten-frame visual ─────────────────────────────────────────────────────────
static void draw_ten_frame(lv_obj_t* parent, int filled) {
    const int CW = 56, CH = 56, GAP = 10;
    const int COLS = 5, ROWS = 2;
    const int total_w = COLS * CW + (COLS-1) * GAP;
    const int total_h = ROWS * CH + (ROWS-1) * GAP;
    lv_obj_t* frame = lv_obj_create(parent);
    lv_obj_set_size(frame, total_w + 28, total_h + 28);
    lv_obj_set_style_bg_color(frame, lv_color_hex(C_HEADER), 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(frame, 18, 0);
    lv_obj_set_style_border_width(frame, 0, 0);
    lv_obj_set_style_pad_all(frame, 14, 0);
    lv_obj_remove_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(frame);
    int cell = 0;
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            lv_obj_t* dot = lv_obj_create(frame);
            lv_obj_set_size(dot, CW, CH);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(dot, 2, 0);
            lv_obj_set_style_border_color(dot, lv_color_hex(C_SUBTEXT), 0);
            bool f = (cell < filled);
            lv_obj_set_style_bg_color(dot,
                lv_color_hex(f ? C_GOLD : C_BTN), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_pos(dot, c*(CW+GAP), r*(CH+GAP));
            cell++;
        }
    }
}

// ── Dot visual (count questions) ──────────────────────────────────────────────
static void draw_dots(lv_obj_t* parent, int count) {
    if (count <= 0 || count > 12) return;
    const int D = 64, GAP = 14;
    int cols = (count <= 4) ? count : (count <= 8) ? 4 : 5;
    int rows = (count + cols - 1) / cols;
    int total_w = cols * D + (cols-1) * GAP;
    int total_h = rows * D + (rows-1) * GAP;
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_set_size(box, total_w + 28, total_h + 28);
    lv_obj_set_style_bg_color(box, lv_color_hex(C_HEADER), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 18, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 14, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(box);
    static const uint32_t COLORS[] = {
        0xFF6B35, 0xFFD166, 0x06D6A0, 0x4ECDC4, 0xEF476F, 0xA66DD4
    };
    for (int i = 0; i < count; i++) {
        lv_obj_t* d = lv_obj_create(box);
        lv_obj_set_size(d, D, D);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(COLORS[i % 6]), 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        int col = i % cols, row = i / cols;
        lv_obj_set_pos(d, col*(D+GAP), row*(D+GAP));
    }
}

// Bottom-center semi-transparent "Home" button for in-game screens.
// ~120x50px, soft so it never competes with answer buttons; taps -> launcher.
static void on_back_to_launcher(lv_event_t* e);
static void add_home_button(lv_obj_t* scr) {
    lv_obj_t* hb = lv_button_create(scr);
    lv_obj_set_size(hb, 120, 50);
    lv_obj_align(hb, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(hb, lv_color_hex(C_BTN), 0);
    lv_obj_set_style_bg_opa(hb, LV_OPA_40, 0);
    lv_obj_set_style_bg_color(hb, lv_color_hex(C_BTN_PRESS), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(hb, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_radius(hb, 16, 0);
    lv_obj_set_style_border_width(hb, 0, 0);
    lv_obj_set_style_shadow_width(hb, 0, 0);
    lv_obj_t* hl = lv_label_create(hb);
    lv_label_set_text(hl, "Home");
    lv_obj_set_style_text_font(hl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(hl, lv_color_hex(C_CARD_TXT), 0);
    lv_obj_set_style_text_opa(hl, LV_OPA_80, 0);
    lv_obj_center(hl);
    lv_obj_add_event_cb(hb, on_back_to_launcher, LV_EVENT_CLICKED, NULL);
}

// ── SCREEN: HOME ─────────────────────────────────────────────────────────────
static void on_launch_arcade(lv_event_t* e) { launch_arcade(); }
static void on_launch_settings(lv_event_t* e) { launch_settings(); }

static lv_obj_t* make_app_tile(lv_obj_t* parent, const char* icon,
                                const char* name, uint32_t bg, uint32_t bg_dk,
                                int x, int y, int w, int h,
                                lv_event_cb_t cb) {
    lv_obj_t* t = lv_obj_create(parent);
    lv_obj_set_size(t, w, h);
    lv_obj_set_pos(t, x, y);
    lv_obj_set_style_bg_color(t, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_grad_color(t, lv_color_hex(bg_dk), 0);
    lv_obj_set_style_bg_grad_dir(t, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(t, 32, 0);
    lv_obj_set_style_border_width(t, 1, 0);
    lv_obj_set_style_border_color(t, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(t, 38, 0);
    lv_obj_set_style_shadow_width(t, 36, 0);
    lv_obj_set_style_shadow_color(t, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(t, 140, 0);
    lv_obj_set_style_shadow_ofs_y(t, 14, 0);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(t, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* il = make_label(t, icon, &lv_font_montserrat_48, C_CARD_TXT);
    lv_obj_align(il, LV_ALIGN_CENTER, 0, -24);
    lv_obj_t* nl = make_label(t, name, &lv_font_montserrat_28, C_CARD_TXT);
    lv_obj_align(nl, LV_ALIGN_CENTER, 0, 48);
    return t;
}

// ── SCREEN: LAUNCHER (game + settings) ───────────────────────────────────────
static void on_math_tap(lv_event_t* e) {
    g_game = 0;
    build_round();
    prepare_question();
    show_question();
}
static void on_read_tap(lv_event_t* e) {
    g_game = 1;
    build_round();
    prepare_question();
    show_question();
}
// Live brightness drag; persist to NVS when the gesture settles (RELEASED).
static void on_brightness_slider(lv_event_t* e) {
    lv_obj_t* sld   = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* vlbl  = (lv_obj_t*)lv_event_get_user_data(e);
    int v = lv_slider_get_value(sld);
    set_brightness((uint8_t)v);
    if (vlbl) {
        char b[24];
        snprintf(b, sizeof(b), "%d%%", (int)g_brightness);
        lv_label_set_text(vlbl, b);
    }
    brightness_save();
}

static void on_launcher_longpress(lv_event_t* e) {
    g_pin_len = 0;
    g_pin_buf[0] = '\0';
    show_pin();
}

// Build one big tappable game card with title + subtitle, vertically centered.
static lv_obj_t* make_game_card(lv_obj_t* parent, const char* title,
                                const char* subtitle, uint32_t bg,
                                lv_event_cb_t cb) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, SCR_W - 60, 360);
    lv_obj_set_style_bg_color(card, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 32, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 28, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card, 90, 0);
    lv_obj_set_style_shadow_ofs_y(card, 8, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(card, 18, 0);

    lv_obj_t* t = make_label(card, title, &lv_font_montserrat_48, C_CARD_TXT);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t* s = make_label(card, subtitle, &lv_font_montserrat_24, C_CARD_TXT);
    lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_opa(s, LV_OPA_80, 0);
    return card;
}

static void show_home(void) {
    lvgl_port_lock(0);

    lv_obj_t* scr = lv_obj_create(NULL);
    // Deep gradient background — not flat, has atmosphere
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0D0120), 0);
    lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0x1C0848), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Decorative orbs — created first so they sit behind all content
    lv_obj_t* orb1 = lv_obj_create(scr);
    lv_obj_set_size(orb1, 400, 400);
    lv_obj_set_pos(orb1, SCR_W - 160, -140);
    lv_obj_set_style_radius(orb1, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(orb1, lv_color_hex(0x7B2FFF), 0);
    lv_obj_set_style_bg_opa(orb1, 28, 0);
    lv_obj_set_style_border_width(orb1, 0, 0);
    lv_obj_set_style_shadow_width(orb1, 0, 0);
    lv_obj_remove_flag(orb1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(orb1, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* orb2 = lv_obj_create(scr);
    lv_obj_set_size(orb2, 320, 320);
    lv_obj_set_pos(orb2, -110, SCR_H - 240);
    lv_obj_set_style_radius(orb2, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(orb2, lv_color_hex(0x0044FF), 0);
    lv_obj_set_style_bg_opa(orb2, 22, 0);
    lv_obj_set_style_border_width(orb2, 0, 0);
    lv_obj_set_style_shadow_width(orb2, 0, 0);
    lv_obj_remove_flag(orb2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(orb2, LV_OBJ_FLAG_CLICKABLE);

    // Status bar: transparent — floats over gradient, long-press → admin PIN
    lv_obj_t* hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, SCR_W, 100);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_border_color(hdr, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(hdr, 18, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hdr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hdr, on_launcher_longpress, LV_EVENT_LONG_PRESSED, NULL);

    char star_buf[32];
    snprintf(star_buf, sizeof(star_buf), "* %ld", (long)g_stars);
    lv_obj_t* star_lbl = make_label(hdr, star_buf, &lv_font_montserrat_24, C_STAR);
    lv_obj_align(star_lbl, LV_ALIGN_LEFT_MID, 24, 0);

    lv_obj_t* ttl = make_label(hdr, "Kid Arcade", &lv_font_montserrat_32, C_GOLD);
    lv_obj_align(ttl, LV_ALIGN_CENTER, 0, 0);

    // App grid: 2 tiles side by side, vertically centered in available space
    const int TILE_W = 260, TILE_H = 300, TILE_GAP = 28;
    const int GRID_W  = TILE_W * 2 + TILE_GAP;
    const int CONTENT_H = SCR_H - 100;
    const int GRID_Y  = 100 + (CONTENT_H - TILE_H) / 3;  // upper-third of content

    lv_obj_t* grid = lv_obj_create(scr);
    lv_obj_set_size(grid, GRID_W, TILE_H);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, GRID_Y);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    make_app_tile(grid, "A", "Arcade",
                  C_MATH, C_MATH_DK, 0, 0, TILE_W, TILE_H, on_launch_arcade);
    make_app_tile(grid, "S", "Settings",
                  C_BTN_ALT, 0x4A339A, TILE_W + TILE_GAP, 0, TILE_W, TILE_H, on_launch_settings);

    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, true);
    lvgl_port_unlock();
}

static void launch_arcade(void) {
    lvgl_port_lock(0);

    lv_obj_t* scr = lv_obj_create(NULL);
    style_screen(scr);

    lv_obj_t* hdr = make_header(scr, 88);

    lv_obj_t* back_btn = lv_button_create(hdr);
    lv_obj_set_size(back_btn, 64, 56);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(C_BTN), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(C_BTN_PRESS), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 14, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, "<");
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(C_CARD_TXT), 0);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(back_btn, on_back_to_launcher, LV_EVENT_CLICKED, NULL);

    lv_obj_t* hl = make_label(hdr, "Choose a Game",
                              &lv_font_montserrat_32, C_GOLD);
    lv_obj_align(hl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* col = make_col(scr, SCR_W, SCR_H - 88, 36);
    lv_obj_align(col, LV_ALIGN_TOP_MID, 0, 88);

    make_game_card(col, "MATH", "Count   Add   Skip   Multiply",
                   C_MATH, on_math_tap);
    make_game_card(col, "READING", "Letters   Words   Rhymes",
                   C_READ, on_read_tap);

    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, true);
    lvgl_port_unlock();
}

static void launch_settings(void) {
    lvgl_port_lock(0);

    lv_obj_t* scr = lv_obj_create(NULL);
    style_screen(scr);

    lv_obj_t* hdr = make_header(scr, 88);

    lv_obj_t* back_btn = lv_button_create(hdr);
    lv_obj_set_size(back_btn, 64, 56);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(C_BTN), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(C_BTN_PRESS), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 14, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    lv_obj_t* back_lbl2 = lv_label_create(back_btn);
    lv_label_set_text(back_lbl2, "<");
    lv_obj_set_style_text_font(back_lbl2, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(back_lbl2, lv_color_hex(C_CARD_TXT), 0);
    lv_obj_center(back_lbl2);
    lv_obj_add_event_cb(back_btn, on_back_to_launcher, LV_EVENT_CLICKED, NULL);

    lv_obj_t* hl2 = make_label(hdr, "Settings", &lv_font_montserrat_32, C_GOLD);
    lv_obj_align(hl2, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* col = make_col(scr, SCR_W - 40, SCR_H - 88, 34);
    lv_obj_align(col, LV_ALIGN_TOP_MID, 0, 88);

    lv_obj_t* br_lbl = make_label(col, "Brightness",
                                  &lv_font_montserrat_32, C_CARD_TXT);
    lv_obj_set_style_text_align(br_lbl, LV_TEXT_ALIGN_CENTER, 0);

    char br_buf[24];
    snprintf(br_buf, sizeof(br_buf), "%d%%", (int)g_brightness);
    lv_obj_t* br_val = make_label(col, br_buf,
                                  &lv_font_montserrat_48, C_STAR);
    lv_obj_set_style_text_align(br_val, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t* sld = lv_slider_create(col);
    lv_obj_set_size(sld, SCR_W - 120, 40);
    lv_slider_set_range(sld, BRIGHTNESS_MIN, 100);
    lv_slider_set_value(sld, g_brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sld, lv_color_hex(C_BTN), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sld, lv_color_hex(C_GOLD), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sld, lv_color_hex(C_STAR), LV_PART_KNOB);
    lv_obj_set_style_pad_all(sld, 10, LV_PART_KNOB);
    lv_obj_add_event_cb(sld, on_brightness_slider, LV_EVENT_VALUE_CHANGED, br_val);

    char total[40];
    snprintf(total, sizeof(total), "Total stars: %ld", (long)g_stars);
    lv_obj_t* t_lbl = make_label(col, total, &lv_font_montserrat_28, C_SUBTEXT);
    lv_obj_set_style_text_align(t_lbl, LV_TEXT_ALIGN_CENTER, 0);

    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 250, 0, true);
    lvgl_port_unlock();
}

// ── SCREEN: QUESTION ─────────────────────────────────────────────────────────
static void on_answer(lv_event_t* e) {
    int tapped = (int)(intptr_t)lv_event_get_user_data(e);
    g_last_correct = (tapped == g_qd.correct_idx);
    if (g_last_correct) {
        g_stars++;
        g_stars_round++;
        nvs_save_stars();
    }
    show_feedback();
}
static void on_back_to_launcher(lv_event_t* e) { show_home(); }

static void show_question(void) {
    const char* game_name = (g_game == 0) ? "Math" : "Reading";
    uint32_t    game_col  = (g_game == 0) ? C_MATH : C_READ;

    lvgl_port_lock(0);

    lv_obj_t* scr = lv_obj_create(NULL);
    style_screen(scr);

    // Header: back button (left), game name (center), progress (right).
    lv_obj_t* hdr = make_header(scr, 88);

    lv_obj_t* back_btn = lv_button_create(hdr);
    lv_obj_set_size(back_btn, 64, 56);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(C_BTN), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(C_BTN_PRESS),
                              LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 14, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, "<");
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(C_CARD_TXT), 0);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(back_btn, on_back_to_launcher, LV_EVENT_CLICKED, NULL);

    lv_obj_t* hl = make_label(hdr, game_name,
                              &lv_font_montserrat_32, game_col);
    lv_obj_align(hl, LV_ALIGN_CENTER, 0, 0);

    char prog[20];
    snprintf(prog, sizeof(prog), "Q %d/%d", g_q_idx+1, QUESTIONS_PER_ROUND);
    lv_obj_t* pl = make_label(hdr, prog, &lv_font_montserrat_24, C_SUBTEXT);
    lv_obj_align(pl, LV_ALIGN_RIGHT_MID, -18, 0);

    // ── Middle content region (between header and answer stack) ──────────────
    const int BTN_H = 110, BTN_GAP = 16, BTN_MARGIN = 30;
    const int STACK_H = 3 * BTN_H + 2 * BTN_GAP;            // 362
    const int STACK_TOP = SCR_H - STACK_H - 24;             // y of first button
    const int MID_TOP = 88 + 16;
    const int MID_H = STACK_TOP - MID_TOP - 16;

    lv_obj_t* mid = make_col(scr, SCR_W - 40, MID_H, 24);
    lv_obj_align(mid, LV_ALIGN_TOP_MID, 0, MID_TOP);

    lv_obj_t* q_lbl = lv_label_create(mid);
    lv_label_set_text(q_lbl, g_qd.prompt);
    lv_obj_set_style_text_font(q_lbl, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(q_lbl, lv_color_hex(C_CARD_TXT), 0);
    lv_obj_set_style_text_align(q_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(q_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(q_lbl, SCR_W - 60);

    if (g_qd.extra[0]) {
        lv_obj_t* ex = lv_label_create(mid);
        lv_label_set_text(ex, g_qd.extra);
        lv_obj_set_style_text_font(ex, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(ex, lv_color_hex(C_GOLD), 0);
        lv_obj_set_style_text_align(ex, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(ex, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(ex, SCR_W - 60);
    }

    if (g_qd.vis_type == 1) {
        draw_dots(mid, g_qd.vis_count);
    } else if (g_qd.vis_type == 2) {
        draw_ten_frame(mid, g_qd.vis_count);
    }

    // ── Answer buttons: 3 full-width stacked at the bottom ───────────────────
    static const uint32_t BTN_COLS[3] = { 0x3A2A7A, 0x4A2F8C, 0x5A3A9E };
    for (int i = 0; i < 3; i++) {
        lv_obj_t* btn = lv_button_create(scr);
        lv_obj_set_size(btn, SCR_W - 2*BTN_MARGIN, BTN_H);
        lv_obj_set_pos(btn, BTN_MARGIN, STACK_TOP + i*(BTN_H+BTN_GAP));
        lv_obj_set_style_bg_color(btn, lv_color_hex(BTN_COLS[i]), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(C_BTN_PRESS),
                                  LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 22, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 14, 0);
        lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(btn, 70, 0);
        lv_obj_set_style_shadow_ofs_y(btn, 5, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, g_qd.opts[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(C_CARD_TXT), 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, on_answer, LV_EVENT_CLICKED,
                            (void*)(intptr_t)i);
    }

    add_home_button(scr);

    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, true);
    lvgl_port_unlock();
}

// ── SCREEN: FEEDBACK ─────────────────────────────────────────────────────────
static void on_next_question(lv_event_t* e) {
    if (!g_last_correct) {
        show_question();
        return;
    }
    g_q_idx++;
    if (g_q_idx >= QUESTIONS_PER_ROUND) {
        show_round_complete();
    } else {
        prepare_question();
        show_question();
    }
}

static void show_feedback(void) {
    lvgl_port_lock(0);

    // Full-screen color wash: green = correct, warm amber = wrong (soft).
    uint32_t wash    = g_last_correct ? C_CORRECT : C_WRONG;
    uint32_t btn_clr = g_last_correct ? C_CORRECT_DK : C_WRONG_DK;

    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(wash), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* col = make_col(scr, SCR_W - 60, SCR_H, 30);
    lv_obj_center(col);

    lv_obj_t* big = make_label(col,
        g_last_correct ? "Correct!" : "Try Again!",
        &lv_font_montserrat_48, 0xFFFFFF);
    lv_obj_set_style_text_align(big, LV_TEXT_ALIGN_CENTER, 0);

    if (g_last_correct) {
        char msg[48];
        snprintf(msg, sizeof(msg), "You earned a star!   * %ld",
                 (long)g_stars);
        lv_obj_t* sub = make_label(col, msg,
                                   &lv_font_montserrat_28, 0xFFFFFF);
        lv_obj_set_style_opa(sub, LV_OPA_90, 0);
        lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    } else {
        lv_obj_t* sub = make_label(col, "Give it another go!",
                                   &lv_font_montserrat_28, 0xFFFFFF);
        lv_obj_set_style_opa(sub, LV_OPA_90, 0);
        lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    }

    // Spacer so the button sits clearly below the text.
    lv_obj_t* spacer = lv_obj_create(col);
    lv_obj_set_size(spacer, 1, 30);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);

    make_btn(col, g_last_correct ? "Next" : "Try Again",
             360, 110, btn_clr, on_next_question, NULL);

    add_home_button(scr);

    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, true);
    lvgl_port_unlock();
}

// ── SCREEN: ROUND COMPLETE ───────────────────────────────────────────────────
static void on_play_again(lv_event_t* e) {
    build_round();
    prepare_question();
    show_question();
}
static void on_home(lv_event_t* e) { show_home(); }

static void show_round_complete(void) {
    lvgl_port_lock(0);

    lv_obj_t* scr = lv_obj_create(NULL);
    style_screen(scr);

    lv_obj_t* col = make_col(scr, SCR_W - 60, SCR_H, 26);
    lv_obj_center(col);

    make_label(col, "Round Done!", &lv_font_montserrat_48, C_GOLD);

    // Visual star row for stars earned this round.
    char stars_str[32] = "";
    for (int i = 0; i < g_stars_round && i < 5; i++)
        strncat(stars_str, "* ", sizeof(stars_str)-strlen(stars_str)-1);
    if (!stars_str[0]) strncpy(stars_str, "-", sizeof(stars_str)-1);
    lv_obj_t* star_row = make_label(col, stars_str,
                                    &lv_font_montserrat_48, C_STAR);
    lv_obj_set_style_text_align(star_row, LV_TEXT_ALIGN_CENTER, 0);

    char earned[48];
    snprintf(earned, sizeof(earned), "+%d this round", g_stars_round);
    lv_obj_t* e_lbl = make_label(col, earned,
                                 &lv_font_montserrat_32, C_CORRECT);
    lv_obj_set_style_text_align(e_lbl, LV_TEXT_ALIGN_CENTER, 0);

    char total[40];
    snprintf(total, sizeof(total), "Total stars: %ld", (long)g_stars);
    lv_obj_t* t_lbl = make_label(col, total,
                                 &lv_font_montserrat_28, C_SUBTEXT);
    lv_obj_set_style_text_align(t_lbl, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t* spacer = lv_obj_create(col);
    lv_obj_set_size(spacer, 1, 24);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);

    make_btn(col, "Play Again", SCR_W - 80, 110, C_MATH,
             on_play_again, NULL);
    make_btn(col, "Home", SCR_W - 80, 90, C_BTN, on_home, NULL);

    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, true);
    lvgl_port_unlock();
}

// ── SCREEN: PIN ──────────────────────────────────────────────────────────────
static lv_obj_t* g_pin_display = NULL;

static void update_pin_display(void) {
    if (!g_pin_display) return;
    char masked[24] = "";
    for (int i = 0; i < g_pin_len; i++)
        strncat(masked, "*  ", sizeof(masked)-strlen(masked)-1);
    lv_label_set_text(g_pin_display, masked[0] ? masked : "_  _  _  _");
}

static void on_pin_digit(lv_event_t* e) {
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    if (d == -1) {
        if (g_pin_len > 0) g_pin_buf[--g_pin_len] = '\0';
    } else if (d == -2) {
        show_home();
        return;
    } else {
        if (g_pin_len < 4) g_pin_buf[g_pin_len++] = '0' + d;
        g_pin_buf[g_pin_len] = '\0';
        if (g_pin_len == 4) {
            if (strcmp(g_pin_buf, ADMIN_PIN) == 0) {
                show_admin();
                return;
            } else {
                g_pin_len = 0;
                g_pin_buf[0] = '\0';
                if (g_pin_display)
                    lv_label_set_text(g_pin_display, "Wrong");
                return;
            }
        }
    }
    update_pin_display();
}

static void show_pin(void) {
    lvgl_port_lock(0);
    g_pin_display = NULL;

    lv_obj_t* scr = lv_obj_create(NULL);
    style_screen(scr);

    lv_obj_t* hdr = make_header(scr, 88);
    lv_obj_t* ttl = make_label(hdr, "Enter PIN",
                               &lv_font_montserrat_32, C_GOLD);
    lv_obj_center(ttl);

    g_pin_display = lv_label_create(scr);
    lv_label_set_text(g_pin_display, "_  _  _  _");
    lv_obj_set_style_text_font(g_pin_display, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(g_pin_display, lv_color_hex(C_GOLD), 0);
    lv_obj_align(g_pin_display, LV_ALIGN_TOP_MID, 0, 140);

    // 3×4 numpad centered with flex column of rows.
    const int BW = 160, BH = 110, GAP = 16;
    lv_obj_t* pad = lv_obj_create(scr);
    int pad_w = 3*BW + 2*GAP;
    int pad_h = 4*BH + 3*GAP;
    lv_obj_set_size(pad, pad_w, pad_h);
    lv_obj_set_style_bg_opa(pad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pad, 0, 0);
    lv_obj_set_style_pad_all(pad, 0, 0);
    lv_obj_remove_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(pad, LV_ALIGN_TOP_MID, 0, 260);

    int digits[12]       = {1,2,3,4,5,6,7,8,9,-1,0,-2};
    const char* labels[12] = {"1","2","3","4","5","6","7","8","9","<","0","X"};
    for (int i = 0; i < 12; i++) {
        int col = i % 3, row = i / 3;
        uint32_t clr = (i == 11) ? C_DANGER
                       : (i == 9 ? C_BTN_ALT : C_BTN);
        lv_obj_t* btn = make_btn(pad, labels[i], BW, BH, clr,
                                 on_pin_digit, (void*)(intptr_t)digits[i]);
        lv_obj_set_pos(btn, col*(BW+GAP), row*(BH+GAP));
    }

    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, true);
    lvgl_port_unlock();
}

// ── SCREEN: ADMIN ────────────────────────────────────────────────────────────
static void on_reset_stars(lv_event_t* e) {
    g_stars = 0;
    nvs_save_stars();
    show_home();
}
static void on_add50(lv_event_t* e) {
    g_stars += 50;
    nvs_save_stars();
    show_home();
}
static void on_admin_home(lv_event_t* e) { show_home(); }

static void show_admin(void) {
    lvgl_port_lock(0);

    lv_obj_t* scr = lv_obj_create(NULL);
    style_screen(scr);

    lv_obj_t* hdr = make_header(scr, 88);
    lv_obj_t* ttl = make_label(hdr, "Admin",
                               &lv_font_montserrat_32, C_GOLD);
    lv_obj_center(ttl);

    lv_obj_t* col = make_col(scr, SCR_W - 60, SCR_H - 88, 26);
    lv_obj_align(col, LV_ALIGN_TOP_MID, 0, 88);

    char total[40];
    snprintf(total, sizeof(total), "Stars: %ld", (long)g_stars);
    lv_obj_t* tl = make_label(col, total,
                              &lv_font_montserrat_32, C_STAR);
    lv_obj_set_style_text_align(tl, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t* spacer = lv_obj_create(col);
    lv_obj_set_size(spacer, 1, 20);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);

    make_btn(col, "+50 Stars", SCR_W - 90, 110, C_CORRECT,
             on_add50, NULL);
    make_btn(col, "Reset Stars", SCR_W - 90, 110, C_DANGER,
             on_reset_stars, NULL);
    make_btn(col, "Home", SCR_W - 90, 90, C_BTN,
             on_admin_home, NULL);

    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, true);
    lvgl_port_unlock();
}

// ── LDO / DSI PHY power ──────────────────────────────────────────────────────
static void enable_dsi_phy_power(void) {
    static esp_ldo_channel_handle_t phy_pwr_chan = NULL;
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id    = MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan));
}

// ── app_main ──────────────────────────────────────────────────────────────────
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "kid_arcade ESP32-P4 starting");

    // NVS
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    nvs_load_stars();
    brightness_load();
    brightness_init();    // LEDC PWM backlight, duty 0 until display is up

    // (backlight handled by LEDC; configured before display init)

    enable_dsi_phy_power();

    // MIPI DSI bus
    esp_lcd_dsi_bus_handle_t dsi_bus;
    esp_lcd_dsi_bus_config_t dsi_bus_cfg = {};
    dsi_bus_cfg.bus_id             = 0;
    dsi_bus_cfg.num_data_lanes     = 2;
    dsi_bus_cfg.phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
    dsi_bus_cfg.lane_bit_rate_mbps = 900;
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&dsi_bus_cfg, &dsi_bus));

    // DBI panel IO
    esp_lcd_panel_io_handle_t panel_io;
    esp_lcd_dbi_io_config_t dbi_cfg = EK79007_PANEL_IO_DBI_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &panel_io));

    // DPI config
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

    // EK79007 panel
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

    // LVGL port
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_stack = 8192;
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    // Add display
    lvgl_port_display_cfg_t disp_cfg = {};
    disp_cfg.io_handle    = panel_io;
    disp_cfg.panel_handle = panel;
    disp_cfg.buffer_size  = DISPLAY_WIDTH * 50;
    disp_cfg.double_buffer = false;
    disp_cfg.hres          = DISPLAY_WIDTH;
    disp_cfg.vres          = DISPLAY_HEIGHT;
    disp_cfg.monochrome    = false;
    disp_cfg.color_format  = LV_COLOR_FORMAT_RGB565;
    disp_cfg.flags.buff_dma    = 1;
    disp_cfg.flags.buff_spiram = 0;
    disp_cfg.flags.sw_rotate   = 1;
    lvgl_port_display_dsi_cfg_t dsi_disp_cfg = {
        .flags = { .avoid_tearing = 0 },
    };
    lv_display_t *disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_disp_cfg);
    assert(disp);

    lvgl_port_lock(0);
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);
    lvgl_port_unlock();

    // GT911 touch
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

    // Backlight on — ramp to saved brightness now that the panel is live.
    set_brightness(g_brightness);
    ESP_LOGI(TAG, "Display up — launching game");

    show_home();
}
