/*
 * kid_arcade — ESP32-P4 CrowPanel 7" (1024×600 MIPI DSI, portrait 600×1024)
 * Full game: Math + Reading, app-launcher UI, LVGL 9, IDF 6.0.1
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
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

// ── Game config ───────────────────────────────────────────────────────────────
#define QUESTIONS_PER_ROUND 5
#define ADMIN_PIN           "0000"

// ── Colors ────────────────────────────────────────────────────────────────────
#define C_BG        0x0F0E17
#define C_GOLD      0xFFD700
#define C_MATH      0xE85D04
#define C_READ      0x1B4FD8
#define C_CORRECT   0x2DC653
#define C_WRONG     0xFF6B35
#define C_HEADER    0x16213E
#define C_CARD_TXT  0xFFFFFF
#define C_BTN       0x2A2A3E
#define C_BTN_PRESS 0x3D3D5C
#define C_STAR      0xFFD700

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
static void show_launcher(void);
static void show_question(void);
static void show_feedback(void);
static void show_round_complete(void);
static void show_pin(void);
static void show_admin(void);

// ── Common style helpers ──────────────────────────────────────────────────────
static void style_screen(lv_obj_t* scr) {
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
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

static lv_obj_t* make_btn(lv_obj_t* parent, const char* txt,
                           int w, int h, uint32_t bg,
                           lv_event_cb_t cb, void* ud) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(C_BTN_PRESS), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 20, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 8, 0);
    lv_obj_set_style_shadow_opa(btn, 80, 0);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl);
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);
    return btn;
}

// ── Ten-frame visual ─────────────────────────────────────────────────────────
static void draw_ten_frame(lv_obj_t* parent, int filled) {
    // 2 rows × 5 cols of 46×46 circles, 8px gap
    const int CW = 46, CH = 46, GAP = 8;
    const int COLS = 5, ROWS = 2;
    const int total_w = COLS * CW + (COLS-1) * GAP;
    const int total_h = ROWS * CH + (ROWS-1) * GAP;
    lv_obj_t* frame = lv_obj_create(parent);
    lv_obj_set_size(frame, total_w + 20, total_h + 20);
    lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(frame, 0, 0);
    lv_obj_remove_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(frame);
    int cell = 0;
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            lv_obj_t* dot = lv_obj_create(frame);
            lv_obj_set_size(dot, CW, CH);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(dot, 2, 0);
            lv_obj_set_style_border_color(dot, lv_color_hex(0xAAAAAA), 0);
            bool f = (cell < filled);
            lv_obj_set_style_bg_color(dot, lv_color_hex(f ? 0xFF8C00 : 0x2A2A3E), 0);
            lv_obj_set_pos(dot, 10 + c*(CW+GAP), 10 + r*(CH+GAP));
            cell++;
        }
    }
}

// ── Dot visual (count questions) ──────────────────────────────────────────────
static void draw_dots(lv_obj_t* parent, int count) {
    if (count <= 0 || count > 12) return;
    const int D = 52, GAP = 10;
    int cols = (count <= 4) ? count : (count <= 8) ? 4 : 5;
    int rows = (count + cols - 1) / cols;
    int total_w = cols * D + (cols-1) * GAP;
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_set_size(box, total_w + 20, rows * D + (rows-1) * GAP + 20);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(box);
    static const uint32_t COLORS[] = {0xFF5722,0xE91E63,0x9C27B0,0x2196F3,0x4CAF50,0xFF9800};
    for (int i = 0; i < count; i++) {
        lv_obj_t* d = lv_obj_create(box);
        lv_obj_set_size(d, D, D);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(COLORS[i % 6]), 0);
        int col = i % cols, row = i / cols;
        lv_obj_set_pos(d, 10 + col*(D+GAP), 10 + row*(D+GAP));
    }
}

// ── SCREEN: LAUNCHER ─────────────────────────────────────────────────────────
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
static void on_launcher_longpress(lv_event_t* e) {
    g_pin_len = 0;
    g_pin_buf[0] = '\0';
    show_pin();
}

static void show_launcher(void) {
    lvgl_port_lock(0);

    lv_obj_t* scr = lv_obj_create(NULL);
    style_screen(scr);

    // Header bar
    lv_obj_t* hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, 600, 90);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(C_HEADER), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = make_label(hdr, "Kid Arcade", &lv_font_montserrat_32, C_GOLD);
    lv_obj_set_pos(title, 20, 10);

    char star_buf[32];
    snprintf(star_buf, sizeof(star_buf), "* %ld stars", (long)g_stars);
    lv_obj_t* star_lbl = make_label(hdr, star_buf, &lv_font_montserrat_24, C_STAR);
    lv_obj_set_pos(star_lbl, 20, 52);

    // Long-press on header → admin
    lv_obj_add_event_cb(hdr, on_launcher_longpress, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_flag(hdr, LV_OBJ_FLAG_CLICKABLE);

    // App cards
    const int CARD_W = 540, CARD_H = 390, CARD_X = 30;

    // Math card
    lv_obj_t* math_card = lv_obj_create(scr);
    lv_obj_set_size(math_card, CARD_W, CARD_H);
    lv_obj_set_pos(math_card, CARD_X, 115);
    lv_obj_set_style_bg_color(math_card, lv_color_hex(C_MATH), 0);
    lv_obj_set_style_radius(math_card, 28, 0);
    lv_obj_set_style_border_width(math_card, 0, 0);
    lv_obj_set_style_shadow_width(math_card, 20, 0);
    lv_obj_set_style_shadow_opa(math_card, 100, 0);
    lv_obj_remove_flag(math_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(math_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(math_card, on_math_tap, LV_EVENT_CLICKED, NULL);

    lv_obj_t* ml = make_label(math_card, "MATH", &lv_font_montserrat_48, 0xFFFFFF);
    lv_obj_align(ml, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_t* ms = make_label(math_card, "Count  Add  Skip  Multiply", &lv_font_montserrat_24, 0xFFEEDD);
    lv_obj_align(ms, LV_ALIGN_BOTTOM_MID, 0, -50);

    // Reading card
    lv_obj_t* read_card = lv_obj_create(scr);
    lv_obj_set_size(read_card, CARD_W, CARD_H);
    lv_obj_set_pos(read_card, CARD_X, 525);
    lv_obj_set_style_bg_color(read_card, lv_color_hex(C_READ), 0);
    lv_obj_set_style_radius(read_card, 28, 0);
    lv_obj_set_style_border_width(read_card, 0, 0);
    lv_obj_set_style_shadow_width(read_card, 20, 0);
    lv_obj_set_style_shadow_opa(read_card, 100, 0);
    lv_obj_remove_flag(read_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(read_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(read_card, on_read_tap, LV_EVENT_CLICKED, NULL);

    lv_obj_t* rl = make_label(read_card, "READING", &lv_font_montserrat_48, 0xFFFFFF);
    lv_obj_align(rl, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_t* rs = make_label(read_card, "Letters  Words  Rhymes", &lv_font_montserrat_24, 0xDDEEFF);
    lv_obj_align(rs, LV_ALIGN_BOTTOM_MID, 0, -50);

    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, 250, 0, true);
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
static void on_back_to_launcher(lv_event_t* e) { show_launcher(); }

static void show_question(void) {
    const char* game_name = (g_game == 0) ? "Math" : "Reading";
    uint32_t    game_col  = (g_game == 0) ? C_MATH : C_READ;

    lvgl_port_lock(0);

    lv_obj_t* scr = lv_obj_create(NULL);
    style_screen(scr);

    // Header
    lv_obj_t* hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, 600, 75);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(C_HEADER), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* back_btn = lv_button_create(hdr);
    lv_obj_set_size(back_btn, 70, 50);
    lv_obj_set_pos(back_btn, 8, 12);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333355), 0);
    lv_obj_set_style_radius(back_btn, 12, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, "< ");
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(back_btn, on_back_to_launcher, LV_EVENT_CLICKED, NULL);

    char hdr_txt[40];
    snprintf(hdr_txt, sizeof(hdr_txt), "%s", game_name);
    lv_obj_t* hl = make_label(hdr, hdr_txt, &lv_font_montserrat_28, game_col);
    lv_obj_align(hl, LV_ALIGN_LEFT_MID, 95, 0);

    char prog[20];
    snprintf(prog, sizeof(prog), "Q %d / %d", g_q_idx+1, QUESTIONS_PER_ROUND);
    lv_obj_t* pl = make_label(hdr, prog, &lv_font_montserrat_24, 0xAAAAAA);
    lv_obj_align(pl, LV_ALIGN_RIGHT_MID, -15, 0);

    // Question prompt
    lv_obj_t* q_lbl = lv_label_create(scr);
    lv_label_set_text(q_lbl, g_qd.prompt);
    lv_obj_set_style_text_font(q_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(q_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(q_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(q_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(q_lbl, 560);
    lv_obj_set_pos(q_lbl, 20, 90);

    // Extra text (sequence, word display)
    if (g_qd.extra[0]) {
        lv_obj_t* ex = lv_label_create(scr);
        lv_label_set_text(ex, g_qd.extra);
        lv_obj_set_style_text_font(ex, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(ex, lv_color_hex(C_GOLD), 0);
        lv_obj_set_style_text_align(ex, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(ex, 560);
        lv_obj_set_pos(ex, 20, 165);
    }

    // Visual (dots or ten-frame)
    if (g_qd.vis_type == 1) {
        // Create a container for dots centered in the middle area
        lv_obj_t* vis_cont = lv_obj_create(scr);
        lv_obj_set_size(vis_cont, 560, 220);
        lv_obj_set_pos(vis_cont, 20, 160);
        lv_obj_set_style_bg_opa(vis_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(vis_cont, 0, 0);
        lv_obj_remove_flag(vis_cont, LV_OBJ_FLAG_SCROLLABLE);
        draw_dots(vis_cont, g_qd.vis_count);
    } else if (g_qd.vis_type == 2) {
        lv_obj_t* vis_cont = lv_obj_create(scr);
        lv_obj_set_size(vis_cont, 560, 150);
        lv_obj_set_pos(vis_cont, 20, 170);
        lv_obj_set_style_bg_opa(vis_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(vis_cont, 0, 0);
        lv_obj_remove_flag(vis_cont, LV_OBJ_FLAG_SCROLLABLE);
        draw_ten_frame(vis_cont, g_qd.vis_count);
    }

    // Answer buttons — 3 stacked vertically at bottom
    const int BTN_W = 540, BTN_H = 110, BTN_X = 30, GAP = 14;
    const int BTNS_TOP = 1024 - 3*(BTN_H+GAP) - 20;
    static const uint32_t BTN_COLS[3] = {0x1B4A6B, 0x1B6B3A, 0x5C1B6B};

    for (int i = 0; i < 3; i++) {
        lv_obj_t* btn = lv_button_create(scr);
        lv_obj_set_size(btn, BTN_W, BTN_H);
        lv_obj_set_pos(btn, BTN_X, BTNS_TOP + i*(BTN_H+GAP));
        lv_obj_set_style_bg_color(btn, lv_color_hex(BTN_COLS[i]), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x444466), LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 18, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 10, 0);
        lv_obj_set_style_shadow_opa(btn, 80, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, g_qd.opts[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, on_answer, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }

    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, true);
    lvgl_port_unlock();
}

// ── SCREEN: FEEDBACK ─────────────────────────────────────────────────────────
static void on_next_question(lv_event_t* e) {
    if (!g_last_correct) {
        // retry same question
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

    lv_obj_t* scr = lv_obj_create(NULL);
    uint32_t bg = g_last_correct ? 0x0A3320 : 0x2A1500;
    lv_obj_set_style_bg_color(scr, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* icon = make_label(scr,
        g_last_correct ? "Correct!" : "Try Again!",
        &lv_font_montserrat_48,
        g_last_correct ? C_CORRECT : C_WRONG);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -120);

    if (g_last_correct) {
        char msg[40];
        snprintf(msg, sizeof(msg), "You earned a star!  Total: %ld", (long)g_stars);
        lv_obj_t* sub = make_label(scr, msg, &lv_font_montserrat_24, C_STAR);
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, -50);
    }

    const char* btn_txt = g_last_correct ? "Next" : "Try Again";
    lv_obj_t* btn = make_btn(scr, btn_txt, 320, 110,
        g_last_correct ? C_CORRECT : C_WRONG,
        on_next_question, NULL);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 100);

    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, true);
    lvgl_port_unlock();
}

// ── SCREEN: ROUND COMPLETE ───────────────────────────────────────────────────
static void on_play_again(lv_event_t* e) {
    build_round();
    prepare_question();
    show_question();
}
static void on_home(lv_event_t* e) { show_launcher(); }

static void show_round_complete(void) {
    lvgl_port_lock(0);

    lv_obj_t* scr = lv_obj_create(NULL);
    style_screen(scr);

    lv_obj_t* ttl = make_label(scr, "Round Done!", &lv_font_montserrat_48, C_GOLD);
    lv_obj_align(ttl, LV_ALIGN_CENTER, 0, -250);

    char earned[60];
    snprintf(earned, sizeof(earned), "+%d stars this round", g_stars_round);
    lv_obj_t* e_lbl = make_label(scr, earned, &lv_font_montserrat_32, C_CORRECT);
    lv_obj_align(e_lbl, LV_ALIGN_CENTER, 0, -160);

    char total[40];
    snprintf(total, sizeof(total), "Total stars: %ld", (long)g_stars);
    lv_obj_t* t_lbl = make_label(scr, total, &lv_font_montserrat_28, C_STAR);
    lv_obj_align(t_lbl, LV_ALIGN_CENTER, 0, -90);

    // Star row
    lv_obj_t* star_row = lv_label_create(scr);
    char stars_str[32] = "";
    for (int i = 0; i < g_stars_round && i < 5; i++) strncat(stars_str, "* ", sizeof(stars_str)-3);
    lv_label_set_text(star_row, stars_str);
    lv_obj_set_style_text_font(star_row, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(star_row, lv_color_hex(C_STAR), 0);
    lv_obj_align(star_row, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* again = make_btn(scr, "Play Again", 400, 110, C_MATH, on_play_again, NULL);
    lv_obj_align(again, LV_ALIGN_CENTER, 0, 150);

    lv_obj_t* home = make_btn(scr, "Home", 260, 90, 0x333355, on_home, NULL);
    lv_obj_align(home, LV_ALIGN_CENTER, 0, 290);

    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, true);
    lvgl_port_unlock();
}

// ── SCREEN: PIN ──────────────────────────────────────────────────────────────
static lv_obj_t* g_pin_display = NULL;

static void update_pin_display(void) {
    if (!g_pin_display) return;
    char masked[8] = "";
    for (int i = 0; i < g_pin_len; i++) strncat(masked, "* ", sizeof(masked)-3);
    lv_label_set_text(g_pin_display, masked[0] ? masked : "____");
}

static void on_pin_digit(lv_event_t* e) {
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    if (d == -1) { // backspace
        if (g_pin_len > 0) g_pin_buf[--g_pin_len] = '\0';
    } else if (d == -2) { // cancel
        show_launcher();
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
                    lv_label_set_text(g_pin_display, "Wrong PIN");
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

    lv_obj_t* ttl = make_label(scr, "Admin PIN", &lv_font_montserrat_32, C_GOLD);
    lv_obj_align(ttl, LV_ALIGN_TOP_MID, 0, 40);

    g_pin_display = lv_label_create(scr);
    lv_label_set_text(g_pin_display, "____");
    lv_obj_set_style_text_font(g_pin_display, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(g_pin_display, lv_color_hex(C_GOLD), 0);
    lv_obj_align(g_pin_display, LV_ALIGN_TOP_MID, 0, 120);

    // 3×4 numpad: 1-9, ←, 0, Cancel
    const int BW = 150, BH = 110, GAPX = 15, GAPY = 15;
    const int START_X = (600 - 3*BW - 2*GAPX) / 2;
    const int START_Y = 250;
    int digits[12] = {1,2,3,4,5,6,7,8,9,-1,0,-2};
    const char* labels[12] = {"1","2","3","4","5","6","7","8","9","<","0","X"};

    for (int i = 0; i < 12; i++) {
        int col = i % 3, row = i / 3;
        uint32_t col_clr = (i == 11) ? 0x662222 : (i == 9 ? 0x334455 : C_BTN);
        lv_obj_t* btn = make_btn(scr, labels[i], BW, BH, col_clr,
                                  on_pin_digit, (void*)(intptr_t)digits[i]);
        lv_obj_set_pos(btn, START_X + col*(BW+GAPX), START_Y + row*(BH+GAPY));
    }

    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, true);
    lvgl_port_unlock();
}

// ── SCREEN: ADMIN ────────────────────────────────────────────────────────────
static void on_reset_stars(lv_event_t* e) {
    g_stars = 0;
    nvs_save_stars();
    show_launcher();
}
static void on_add50(lv_event_t* e) {
    g_stars += 50;
    nvs_save_stars();
    show_launcher();
}
static void on_admin_home(lv_event_t* e) { show_launcher(); }

static void show_admin(void) {
    lvgl_port_lock(0);

    lv_obj_t* scr = lv_obj_create(NULL);
    style_screen(scr);

    lv_obj_t* ttl = make_label(scr, "Admin", &lv_font_montserrat_32, C_GOLD);
    lv_obj_align(ttl, LV_ALIGN_TOP_MID, 0, 60);

    char total[40];
    snprintf(total, sizeof(total), "Stars: %ld", (long)g_stars);
    lv_obj_t* tl = make_label(scr, total, &lv_font_montserrat_28, C_STAR);
    lv_obj_align(tl, LV_ALIGN_TOP_MID, 0, 140);

    lv_obj_t* b1 = make_btn(scr, "+50 Stars", 400, 110, C_CORRECT, on_add50, NULL);
    lv_obj_align(b1, LV_ALIGN_CENTER, 0, -60);

    lv_obj_t* b2 = make_btn(scr, "Reset Stars", 400, 110, 0x882222, on_reset_stars, NULL);
    lv_obj_align(b2, LV_ALIGN_CENTER, 0, 80);

    lv_obj_t* b3 = make_btn(scr, "Home", 280, 90, 0x333355, on_admin_home, NULL);
    lv_obj_align(b3, LV_ALIGN_CENTER, 0, 220);

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

    // Backlight off during init
    gpio_set_direction(DISPLAY_BACKLIGHT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DISPLAY_BACKLIGHT_PIN, 0);

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

    // Backlight on
    gpio_set_level(DISPLAY_BACKLIGHT_PIN, 1);
    ESP_LOGI(TAG, "Display up — launching game");

    show_launcher();
}
