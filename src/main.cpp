#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include <esp_system.h>
#include <SD.h>
#include <ArduinoJson.h>

#include "fonts/NotoSansBold36.h"

TFT_eSPI tft = TFT_eSPI();
Preferences prefs;

constexpr uint16_t RAW_X_LEFT   =  223;
constexpr uint16_t RAW_X_RIGHT  = 3761;
constexpr uint16_t RAW_Y_TOP    = 3700;
constexpr uint16_t RAW_Y_BOTTOM =  264;
constexpr uint16_t TOUCH_Z_THRESHOLD = 600;

constexpr int16_t SCREEN_W = 320;
constexpr int16_t SCREEN_H = 480;
constexpr int     QUESTIONS_PER_ROUND = 5;

// ---------- Layout scaling ----------
// All hardcoded layout numbers below are written for a 320x480 reference panel.
// scaleW / scaleH / scaleMin compile to identity on 320x480 (so the original
// visual layout is preserved byte-for-byte), and proportionally rescale for
// other panel sizes — change SCREEN_W / SCREEN_H above and the whole UI follows.
//
// Caveats: TFT_eSPI built-in fonts (setTextFont 2/4/6) are bitmap fonts at
// fixed pixel sizes and don't scale automatically. For panels very different
// from 320x480 (e.g. 240x320 or 480x800) you may want to revisit font choices.
constexpr int16_t REF_W = 320;
constexpr int16_t REF_H = 480;
constexpr int16_t scaleW(int16_t v) { return (int32_t)v * SCREEN_W / REF_W; }
constexpr int16_t scaleH(int16_t v) { return (int32_t)v * SCREEN_H / REF_H; }
constexpr int16_t scaleMin(int16_t v) {
  return (SCREEN_W * REF_H < SCREEN_H * REF_W)
    ? (int32_t)v * SCREEN_W / REF_W
    : (int32_t)v * SCREEN_H / REF_H;
}

constexpr int16_t       ADMIN_ZONE_X  = SCREEN_W - scaleW(80);
constexpr int16_t       ADMIN_ZONE_Y  = SCREEN_H - scaleH(80);
constexpr unsigned long ADMIN_HOLD_MS          = 2000;
constexpr unsigned long SCREENSAVER_TIMEOUT_MS = 60000;  // 60 s idle → screensaver
constexpr unsigned long SCREENSAVER_FRAME_MS   = 100;    // 10 fps animation
constexpr int           NUM_PARTICLES          = 25;

enum class Screen   { HOME, QUESTION, FEEDBACK, ROUND_COMPLETE, PIN_ENTRY, ADMIN, LOCK, SCREENSAVER };
enum class GameMode { NONE, MATH, READING };
enum class Shape    { APPLE, BALL, STAR, FLOWER, MOON, HEART, TRIANGLE, SQUARE };
enum class QType    { COUNT, MISSING_NUMBER, TEN_FRAME, ADD_UNDER_10, MAKE_TEN,
                      STARTS_WITH, MISSING_LETTER, RHYME, UPPER_LOWER };

Screen   currentScreen        = Screen::HOME;
GameMode currentGameMode      = GameMode::NONE;
int      totalStars           = 0;
int      currentQuestionIndex = 0;
int      correctThisRound     = 0;
int      starsThisRound       = 0;
bool     lastAnswerCorrect    = false;
bool     needsRedraw          = true;
bool     wasTouched           = false;
bool     lockEnabled          = false;
int      mathDifficulty       = 1;   // 0=Easy, 1=Hard
int      readDifficulty       = 1;

unsigned long touchStartMs          = 0;
bool          adminHoldArmed        = false;
unsigned long lastInteractionMs     = 0;
unsigned long lastScreensaverTickMs = 0;

struct Particle { int16_t x, y, px, py; int8_t dx, dy; uint16_t color; uint8_t size; };
Particle particles[NUM_PARTICLES];

const char* const ADMIN_PIN = "0000";  // CHANGE THIS before flashing your own device
char  pinBuffer[5] = "";
int   pinLen       = 0;
bool  pinWrong     = false;

struct RoundEntry { QType type; int idx; };
RoundEntry currentRound[QUESTIONS_PER_ROUND];

// ---------- SD / string pool ----------

constexpr uint8_t SD_CS_PIN = 5;

// Pool for strings loaded from SD card; 4 KB covers ~100 typical question strings.
static char sdStrPool[4096];
static int  sdStrPos = 0;
static const char* poolStr(const char* s) {
  if (!s || !*s) return "";
  int n = strlen(s) + 1;
  if (sdStrPos + n > (int)sizeof(sdStrPool)) return "";
  char* p = sdStrPool + sdStrPos;
  memcpy(p, s, n);
  sdStrPos += n;
  return p;
}

// ---------- Banks ----------

struct CountQ {
  const char* prompt;
  Shape shape;
  int   count;
  int   options[3];
  int   correct;
};
static const CountQ COUNT_DEFAULT[] = {
  {"How many apples?",    Shape::APPLE,    3, {2, 3, 4}, 3},
  {"How many apples?",    Shape::APPLE,    6, {5, 6, 7}, 6},
  {"How many balls?",     Shape::BALL,     2, {1, 2, 3}, 2},
  {"How many balls?",     Shape::BALL,     5, {4, 5, 6}, 5},
  {"How many balls?",     Shape::BALL,     9, {7, 8, 9}, 9},
  {"How many stars?",     Shape::STAR,     4, {3, 4, 5}, 4},
  {"How many stars?",     Shape::STAR,     7, {6, 7, 8}, 7},
  {"How many flowers?",   Shape::FLOWER,   1, {1, 2, 3}, 1},
  {"How many flowers?",   Shape::FLOWER,   8, {6, 8, 9}, 8},
  {"How many moons?",     Shape::MOON,     3, {2, 3, 4}, 3},
  {"How many hearts?",    Shape::HEART,    4, {3, 4, 5}, 4},
  {"How many hearts?",    Shape::HEART,    9, {7, 8, 9}, 9},
  {"How many triangles?", Shape::TRIANGLE, 2, {2, 3, 4}, 2},
  {"How many triangles?", Shape::TRIANGLE, 5, {4, 5, 6}, 5},
  {"How many squares?",   Shape::SQUARE,   3, {2, 3, 4}, 3},
  {"How many squares?",   Shape::SQUARE,   6, {5, 6, 7}, 6},
};
static CountQ countBank[64];
static int    countBankSize = 0;

struct MissingNumQ {
  int seq[4];        // 4 numbers; -1 marks the blank
  int options[3];
  int correctValue;
};
static const MissingNumQ MISNUM_DEFAULT[] = {
  {{ 3,  4, -1,  6}, {2,  5, 7},   5},
  {{ 7,  8,  9, -1}, {7, 10,11}, 10},
  {{-1,  2,  3,  4}, {1,  5, 0},   1},
  {{ 5, -1,  7,  8}, {4,  6, 9},   6},
  {{ 1,  2, -1,  4}, {3,  5, 6},   3},
  {{ 6,  7,  8, -1}, {9, 10, 5},   9},
  {{-1,  9, 10, 11}, {7,  8,12},   8},
  {{12, 13, -1, 15}, {11, 14, 16},14},
  {{17, 18, 19, -1}, {18, 20, 21},20},
  {{-1, 16, 17, 18}, {14, 15, 19},15},
  {{10, -1, 12, 13}, {11, 14, 9}, 11},
};
static MissingNumQ missingNumBank[32];
static int         missingNumBankSize = 0;

struct AddQ {
  int left;
  int right;
  int options[3];
  int correct;
};
static const AddQ ADD_DEFAULT[] = {
  {2, 3, {4, 5, 6},  5},
  {4, 5, {8, 9,10},  9},
  {1, 6, {6, 7, 8},  7},
  {3, 4, {6, 7, 8},  7},
  {2, 7, {8, 9,10},  9},
  {1, 1, {1, 2, 3},  2},
  {5, 4, {7, 8, 9},  9},
  {6, 2, {7, 8, 9},  8},
  {3, 5, {7, 8, 9},  8},
  {2, 6, {7, 8, 9},  8},
  {4, 3, {5, 6, 7},  7},
  {1, 4, {4, 5, 6},  5},
};
static AddQ addBank[64];
static int  addBankSize = 0;

struct Make10Q {
  int left;
  int options[3];
  int correct;
};
static const Make10Q MAKE10_DEFAULT[] = {
  {6, {3, 4, 5},  4},
  {3, {6, 7, 8},  7},
  {7, {2, 3, 4},  3},
  {1, {7, 8, 9},  9},
  {4, {5, 6, 7},  6},
  {8, {1, 2, 3},  2},
  {2, {7, 8, 9},  8},
  {5, {4, 5, 6},  5},
  {9, {0, 1, 2},  1},
  {0, {8, 9,10}, 10},
};
static Make10Q make10Bank[64];
static int     make10BankSize = 0;

struct TenFrameQ {
  int filled;
  int options[3];
  int correct;
};
static const TenFrameQ TENFRAME_DEFAULT[] = {
  {3,  {2, 3, 4}, 3},
  {6,  {5, 6, 7}, 6},
  {8,  {7, 8, 9}, 8},
  {4,  {3, 4, 5}, 4},
  {10, {8, 9,10},10},
  {1,  {1, 2, 3}, 1},
  {5,  {4, 5, 6}, 5},
  {7,  {6, 7, 8}, 7},
};
static TenFrameQ tenFrameBank[32];
static int       tenFrameBankSize = 0;

struct StartsWithQ {
  char        targetLetter;
  const char* options[3];
  int         correctIdx;
};
static const StartsWithQ STARTSWITH_DEFAULT[] = {
  {'A', {"Cat",   "Apple", "Sun"},  1},
  {'B', {"Cat",   "Ball",  "Sun"},  1},
  {'C', {"Cup",   "Pen",   "Hat"},  0},
  {'D', {"Apple", "Duck",  "Car"},  1},
  {'E', {"Egg",   "Cat",   "Sun"},  0},
  {'F', {"Cat",   "Pen",   "Fish"}, 2},
  {'G', {"Gum",   "Cat",   "Pen"},  0},
  {'H', {"Cat",   "Pen",   "Hat"},  2},
  {'I', {"Ice",   "Cat",   "Sun"},  0},
  {'J', {"Cat",   "Jam",   "Pen"},  1},
  {'K', {"Cat",   "Pen",   "Kite"}, 2},
  {'L', {"Pen",   "Cat",   "Lion"}, 2},
  {'M', {"Moon",  "Dog",   "Fish"}, 0},
  {'N', {"Cat",   "Net",   "Pen"},  1},
  {'P', {"Pen",   "Cat",   "Sun"},  0},
  {'R', {"Cat",   "Pen",   "Rain"}, 2},
  {'S', {"Tree",  "Star",  "Book"}, 1},
  {'T', {"Cat",   "Tree",  "Pen"},  1},
};
static StartsWithQ startsWithBank[64];
static int         startsWithBankSize = 0;

struct MissingLetterQ {
  const char* shown;     // e.g. "C_T" -- the '_' is the blank
  char        options[3];
  int         correctIdx;
};
static const MissingLetterQ MISSINGLETTER_DEFAULT[] = {
  {"C_T", {'A', 'O', 'E'}, 0},  // CAT
  {"D_G", {'A', 'O', 'U'}, 1},  // DOG
  {"S_N", {'U', 'I', 'A'}, 0},  // SUN
  {"H_T", {'A', 'O', 'I'}, 0},  // HAT
  {"B_T", {'I', 'A', 'U'}, 1},  // BAT
  {"P_N", {'E', 'O', 'I'}, 0},  // PEN
  {"C_P", {'U', 'A', 'O'}, 0},  // CUP
  {"M_P", {'A', 'I', 'O'}, 0},  // MAP
  {"F_X", {'O', 'A', 'I'}, 0},  // FOX
  {"L_G", {'O', 'E', 'A'}, 0},  // LOG
};
static MissingLetterQ missingLetterBank[64];
static int            missingLetterBankSize = 0;

struct RhymeQ {
  const char* target;
  const char* options[3];
  int         correctIdx;
};
static const RhymeQ RHYME_DEFAULT[] = {
  {"cat",  {"hat",  "dog",   "sun"},  0},
  {"sun",  {"cat",  "fun",   "ball"}, 1},
  {"bug",  {"hat",  "rug",   "pen"},  1},
  {"hop",  {"hat",  "dog",   "top"},  2},
  {"cake", {"lake", "duck",  "sun"},  0},
  {"red",  {"bed",  "pen",   "sun"},  0},
  {"pig",  {"cat",  "big",   "sun"},  1},
  {"fish", {"car",  "dish",  "sun"},  1},
  {"car",  {"jar",  "pen",   "fish"}, 0},
  {"sing", {"sun",  "ring",  "cat"},  1},
};
static RhymeQ rhymeBank[64];
static int    rhymeBankSize = 0;

struct UpperLowerQ {
  char shown;          // lowercase
  char options[3];     // uppercase
  int  correctIdx;
};
static const UpperLowerQ UPPERLOWER_DEFAULT[] = {
  {'b', {'B', 'D', 'P'}, 0},
  {'d', {'B', 'D', 'Q'}, 1},
  {'p', {'B', 'P', 'Q'}, 1},
  {'q', {'P', 'D', 'Q'}, 2},
  {'a', {'A', 'O', 'U'}, 0},
  {'e', {'F', 'E', 'B'}, 1},
  {'g', {'C', 'G', 'Q'}, 1},
  {'m', {'N', 'W', 'M'}, 2},
  {'n', {'M', 'N', 'H'}, 1},
  {'r', {'P', 'B', 'R'}, 2},
  {'s', {'Z', 'S', 'C'}, 1},
  {'t', {'F', 'T', 'L'}, 1},
};
static UpperLowerQ upperLowerBank[64];
static int         upperLowerBankSize = 0;

int bankSizeFor(QType t) {
  switch (t) {
    case QType::COUNT:           return countBankSize;
    case QType::MISSING_NUMBER:  return missingNumBankSize;
    case QType::TEN_FRAME:       return tenFrameBankSize;
    case QType::ADD_UNDER_10:    return addBankSize;
    case QType::MAKE_TEN:        return make10BankSize;
    case QType::STARTS_WITH:     return startsWithBankSize;
    case QType::MISSING_LETTER:  return missingLetterBankSize;
    case QType::RHYME:           return rhymeBankSize;
    case QType::UPPER_LOWER:     return upperLowerBankSize;
  }
  return 0;
}

// ---------- Buttons ----------

struct Button {
  int16_t x, y, w, h;
  const char* label;
  uint16_t fill;
};

const Button BTN_MATH    = { scaleW(40),  scaleH(180), scaleW(240), scaleH(90), "Math Game",    TFT_SKYBLUE };
const Button BTN_READING = { scaleW(40),  scaleH(290), scaleW(240), scaleH(90), "Reading Game", TFT_ORANGE };
const Button BTN_NEXT    = { scaleW(90),  scaleH(370), scaleW(140), scaleH(70), "Next",         TFT_GREEN };
const Button BTN_TRY     = { scaleW(60),  scaleH(370), scaleW(200), scaleH(70), "Try Again",    TFT_ORANGE };
const Button BTN_PLAY    = { scaleW(25),  scaleH(390), scaleW(140), scaleH(70), "Play Again",   TFT_GREEN };
const Button BTN_GOHOME  = { scaleW(175), scaleH(390), scaleW(120), scaleH(70), "Home",         TFT_SKYBLUE };
const Button BTN_RESET       = { scaleW(30), scaleH(115), scaleW(260), scaleH(50), "Reset Stars", TFT_RED      };
const Button BTN_LOCK_TOGGLE = { scaleW(30), scaleH(175), scaleW(260), scaleH(50), "",            TFT_NAVY     };
const Button BTN_MATH_DIFF   = { scaleW(30), scaleH(235), scaleW(260), scaleH(50), "",            TFT_DARKCYAN };
const Button BTN_READ_DIFF   = { scaleW(30), scaleH(295), scaleW(260), scaleH(50), "",            TFT_DARKGREEN};
const Button BTN_CANCEL      = { scaleW(60), scaleH(365), scaleW(200), scaleH(50), "Cancel",      TFT_DARKGREY };

const Button BTN_OPT[3] = {
  { scaleW(20),  scaleH(330), scaleW(80), scaleH(90), "", TFT_PURPLE },
  { scaleW(120), scaleH(330), scaleW(80), scaleH(90), "", TFT_PURPLE },
  { scaleW(220), scaleH(330), scaleW(80), scaleH(90), "", TFT_PURPLE },
};

// PIN entry pad. Columns centred on 320x480: x=45,125,205  w=70  h=55
const Button BTN_PIN_DIGIT[10] = {
  { scaleW(125), scaleH(360), scaleW(70), scaleH(55), "0", TFT_DARKGREY },
  { scaleW( 45), scaleH(155), scaleW(70), scaleH(55), "1", TFT_DARKGREY },
  { scaleW(125), scaleH(155), scaleW(70), scaleH(55), "2", TFT_DARKGREY },
  { scaleW(205), scaleH(155), scaleW(70), scaleH(55), "3", TFT_DARKGREY },
  { scaleW( 45), scaleH(220), scaleW(70), scaleH(55), "4", TFT_DARKGREY },
  { scaleW(125), scaleH(220), scaleW(70), scaleH(55), "5", TFT_DARKGREY },
  { scaleW(205), scaleH(220), scaleW(70), scaleH(55), "6", TFT_DARKGREY },
  { scaleW( 45), scaleH(285), scaleW(70), scaleH(55), "7", TFT_DARKGREY },
  { scaleW(125), scaleH(285), scaleW(70), scaleH(55), "8", TFT_DARKGREY },
  { scaleW(205), scaleH(285), scaleW(70), scaleH(55), "9", TFT_DARKGREY },
};
const Button BTN_PIN_CLEAR  = { scaleW( 45), scaleH(360), scaleW(70), scaleH(55), "C",  TFT_ORANGE   };
const Button BTN_PIN_OK     = { scaleW(205), scaleH(360), scaleW(70), scaleH(55), "OK", TFT_GREEN    };
const Button BTN_PIN_CANCEL = { scaleW(245), scaleH(  8), scaleW(65), scaleH(30), "X",  TFT_DARKGREY };

// Exit-game tap zone in the question-screen header (top-left). Drawn as a
// visible rounded button with a white X icon — big enough to actually hit
// reliably, but in the header strip so it doesn't compete with answer buttons.
const Button BTN_EXIT_GAME = { scaleW(4), scaleH(4), scaleW(54), scaleH(40), "", 0x18C3 /* dim grey */ };

// ---------- Touch helpers ----------

int16_t mapTouchX(uint16_t rx) {
  return constrain(map((int)rx, (int)RAW_X_LEFT, (int)RAW_X_RIGHT, 0, SCREEN_W), 0, SCREEN_W - 1);
}
int16_t mapTouchY(uint16_t ry) {
  return constrain(map((int)ry, (int)RAW_Y_TOP, (int)RAW_Y_BOTTOM, 0, SCREEN_H), 0, SCREEN_H - 1);
}
bool tapInside(const Button& b, int16_t x, int16_t y) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

// ---------- Font helpers ----------

// Anti-aliased ~36px bold for big alpha text — replaces the chunky
// setTextSize(2/3/4) doubling. After useBigFont(), draws use the smooth
// font; useDefaultFont() reverts to the crisp built-in bitmap fonts (Font 2/4/6).
static inline void useBigFont()     { tft.loadFont(NotoSansBold36); }
static inline void useDefaultFont() { tft.unloadFont(); }

// ---------- Drawing primitives ----------

// Halve brightness of an RGB565 color — used for press feedback + button shadows.
static inline uint16_t darken565(uint16_t c) {
  uint16_t r = (c >> 11) & 0x1F;
  uint16_t g = (c >>  5) & 0x3F;
  uint16_t b =  c        & 0x1F;
  return ((r >> 1) << 11) | ((g >> 1) << 5) | (b >> 1);
}

// Brief tactile flash so taps feel responsive. Call right before any
// screen transition; the next redraw paints over the darkened state.
void flashButton(const Button& b) {
  const int16_t r = scaleMin(12);
  uint16_t pressed = darken565(b.fill);
  tft.fillRoundRect(b.x, b.y, b.w, b.h, r, pressed);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, r, TFT_WHITE);
  delay(70);
}

void drawButton(const Button& b) {
  const int16_t r = scaleMin(12);
  tft.fillRoundRect(b.x, b.y, b.w, b.h, r, b.fill);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, r, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, b.fill);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2);
}
void drawNumberButton(const Button& b, int n) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, scaleMin(12), b.fill);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, scaleMin(12), TFT_WHITE);
  tft.setTextColor(TFT_WHITE, b.fill);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(6);
  tft.drawNumber(n, b.x + b.w / 2, b.y + b.h / 2);
}
void drawWordButton(const Button& b, const char* word) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, scaleMin(12), b.fill);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, scaleMin(12), TFT_WHITE);
  tft.setTextColor(TFT_WHITE, b.fill);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.drawString(word, b.x + b.w / 2, b.y + b.h / 2);
}
void drawCharButton(const Button& b, char ch) {
  char buf[2] = { ch, 0 };
  tft.fillRoundRect(b.x, b.y, b.w, b.h, scaleMin(12), b.fill);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, scaleMin(12), TFT_WHITE);
  tft.setTextColor(TFT_WHITE, b.fill);
  tft.setTextDatum(MC_DATUM);
  useBigFont();
  tft.drawString(buf, b.x + b.w / 2, b.y + b.h / 2);
  useDefaultFont();
}

void drawShape(Shape s, int cx, int cy, int r, uint16_t bg) {
  switch (s) {
    case Shape::APPLE:
      tft.fillCircle(cx, cy, r, TFT_RED);
      tft.fillRect(cx - 1, cy - r - 4, 3, 5, TFT_GREEN);
      tft.fillCircle(cx + 4, cy - r - 2, 4, TFT_GREEN);
      break;
    case Shape::BALL:
      tft.fillCircle(cx, cy, r, TFT_BLUE);
      tft.fillCircle(cx - r/3, cy - r/3, r/4, TFT_WHITE);
      break;
    case Shape::STAR: {
      // Real 5-point star, fan-triangulated from the center.
      // Vertices x1000 — outer/inner alternating, point-up, clockwise.
      static const int16_t STAR_PTS[20] = {
           0, -1000,    224,  -309,    951,  -309,    363,   118,
         588,   809,      0,   382,   -588,   809,   -363,   118,
        -951,  -309,   -224,  -309,
      };
      int16_t px[10], py[10];
      for (int i = 0; i < 10; i++) {
        px[i] = cx + (int16_t)((int32_t)STAR_PTS[i*2]     * r / 1000);
        py[i] = cy + (int16_t)((int32_t)STAR_PTS[i*2 + 1] * r / 1000);
      }
      for (int i = 0; i < 10; i++) {
        int j = (i + 1) % 10;
        tft.fillTriangle(cx, cy, px[i], py[i], px[j], py[j], TFT_YELLOW);
      }
      break;
    }
    case Shape::FLOWER:
      tft.fillCircle(cx,                  cy - r,             r/2, TFT_PINK);
      tft.fillCircle(cx + r * 95 / 100,   cy - r * 31 / 100,  r/2, TFT_PINK);
      tft.fillCircle(cx + r * 59 / 100,   cy + r * 81 / 100,  r/2, TFT_PINK);
      tft.fillCircle(cx - r * 59 / 100,   cy + r * 81 / 100,  r/2, TFT_PINK);
      tft.fillCircle(cx - r * 95 / 100,   cy - r * 31 / 100,  r/2, TFT_PINK);
      tft.fillCircle(cx, cy, r/2, TFT_YELLOW);
      break;
    case Shape::MOON:
      tft.fillCircle(cx,         cy, r,         TFT_GOLD);
      tft.fillCircle(cx + r/3,   cy, r * 9 / 10, bg);
      break;
    case Shape::HEART:
      tft.fillCircle(cx - r/2, cy - r/4, r * 6/10, TFT_RED);
      tft.fillCircle(cx + r/2, cy - r/4, r * 6/10, TFT_RED);
      tft.fillTriangle(cx - r, cy + r/6, cx + r, cy + r/6, cx, cy + r, TFT_RED);
      break;
    case Shape::TRIANGLE:
      tft.fillTriangle(cx, cy - r, cx - r, cy + r, cx + r, cy + r, TFT_PURPLE);
      break;
    case Shape::SQUARE:
      tft.fillRect(cx - r * 9/10, cy - r * 9/10, r * 18/10, r * 18/10, TFT_CYAN);
      break;
  }
}

void drawConfettiBackground(uint16_t baseColor, bool avoidHomeButtons) {
  tft.fillScreen(baseColor);
  const uint16_t palette[] = {
    TFT_YELLOW, TFT_PINK, TFT_CYAN, TFT_GREENYELLOW,
    TFT_ORANGE, TFT_MAGENTA, TFT_GOLD, TFT_SKYBLUE
  };
  uint32_t s = 1337;
  auto next = [&]() { s = s * 1103515245u + 12345u; return s >> 16; };
  for (int i = 0; i < 70; i++) {
    int16_t cx = next() % SCREEN_W;
    int16_t cy = next() % SCREEN_H;
    int16_t r  = 2 + (next() % 4);
    uint16_t c = palette[next() % (sizeof(palette) / sizeof(palette[0]))];
    if (avoidHomeButtons) {
      bool overlaps =
        (cx + r >= BTN_MATH.x    && cx - r <= BTN_MATH.x    + BTN_MATH.w    &&
         cy + r >= BTN_MATH.y    && cy - r <= BTN_MATH.y    + BTN_MATH.h) ||
        (cx + r >= BTN_READING.x && cx - r <= BTN_READING.x + BTN_READING.w &&
         cy + r >= BTN_READING.y && cy - r <= BTN_READING.y + BTN_READING.h);
      if (overlaps) continue;
    }
    tft.fillCircle(cx, cy, r, c);
  }
}

void drawHeader(const char* title) {
  // Black header strip stays 32px tall — body content above y=50 is still safe.
  // The exit button is taller and "pops out" past the strip into the body area;
  // it sits in the top-left corner where centred titles never reach.
  const int16_t headerH = scaleH(32);
  tft.fillRect(0, 0, SCREEN_W, headerH, TFT_BLACK);

  // Exit button: dim-grey rounded rect with a thick white X. Extends below the
  // header strip — that's intentional for tap-target size.
  {
    const Button& b = BTN_EXIT_GAME;
    const int16_t r = scaleMin(8);
    tft.fillRoundRect(b.x, b.y, b.w, b.h, r, b.fill);
    tft.drawRoundRect(b.x, b.y, b.w, b.h, r, TFT_LIGHTGREY);
    const int16_t cx = b.x + b.w / 2;
    const int16_t cy = b.y + b.h / 2;
    const int16_t s  = scaleMin(10);
    for (int8_t d = -2; d <= 2; d++) {
      tft.drawLine(cx - s, cy - s + d, cx + s, cy + s + d, TFT_WHITE);
      tft.drawLine(cx + s, cy - s + d, cx - s, cy + s + d, TFT_WHITE);
    }
  }

  // Title — left side of the header, after the exit button's X.
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(ML_DATUM);
  tft.setTextFont(2);
  tft.drawString(title, BTN_EXIT_GAME.x + BTN_EXIT_GAME.w + scaleW(8), headerH / 2);

  // Progress dots: gold = answered correctly, white = current, dark outline = upcoming.
  const int dotR       = scaleMin(5);
  const int dotSpacing = scaleW(15);
  const int rightPad   = scaleW(10);
  const int dotsRight  = SCREEN_W - rightPad - dotR;
  const int dotsY      = headerH / 2;
  for (int i = 0; i < QUESTIONS_PER_ROUND; i++) {
    int dx = dotsRight - (QUESTIONS_PER_ROUND - 1 - i) * dotSpacing;
    if (i < currentQuestionIndex)        tft.fillCircle(dx, dotsY, dotR, TFT_GOLD);
    else if (i == currentQuestionIndex)  tft.fillCircle(dx, dotsY, dotR, TFT_WHITE);
    else                                 tft.drawCircle(dx, dotsY, dotR, TFT_DARKGREY);
  }
}

void drawQuestionFooter(uint16_t bg) {
  tft.setTextDatum(BC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(TFT_LIGHTGREY, bg);
  char buf[32];
  snprintf(buf, sizeof(buf), "Question %d of %d", currentQuestionIndex + 1, QUESTIONS_PER_ROUND);
  tft.drawString(buf, SCREEN_W / 2, SCREEN_H - scaleH(8));
}

// ---------- Per-question-type drawing ----------

void drawCountQ(int idx, uint16_t bg) {
  CountQ& q = countBank[idx];
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextFont(4);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(q.prompt, SCREEN_W / 2, scaleH(50));

  const int radius  = scaleMin(20);
  const int spacing = scaleW(50);
  int firstRow  = (q.count > 5) ? 5 : q.count;
  int secondRow = q.count - firstRow;
  int y1 = (secondRow > 0) ? scaleH(175) : scaleH(200);
  int y2 = scaleH(235);
  int x0a = (SCREEN_W - firstRow * spacing) / 2 + spacing / 2;
  for (int i = 0; i < firstRow; i++) drawShape(q.shape, x0a + i * spacing, y1, radius, bg);
  if (secondRow > 0) {
    int x0b = (SCREEN_W - secondRow * spacing) / 2 + spacing / 2;
    for (int i = 0; i < secondRow; i++) drawShape(q.shape, x0b + i * spacing, y2, radius, bg);
  }
  for (int i = 0; i < 3; i++) drawNumberButton(BTN_OPT[i], q.options[i]);
}

void drawMissingNumQ(int idx, uint16_t bg) {
  MissingNumQ& q = missingNumBank[idx];
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextFont(4);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("What number is missing?", SCREEN_W / 2, scaleH(50));

  const int xPos[4] = { scaleW(50), scaleW(120), scaleW(200), scaleW(270) };
  const int yMid = scaleH(200);
  tft.setTextDatum(MC_DATUM);
  for (int i = 0; i < 4; i++) {
    int v = q.seq[i];
    if (v < 0) {
      tft.fillRect(xPos[i] - scaleW(22), yMid + scaleH(18), scaleW(44), scaleH(8), TFT_YELLOW);
    } else {
      tft.setTextFont(6);
      tft.setTextColor(TFT_WHITE, bg);
      tft.drawNumber(v, xPos[i], yMid);
    }
  }
  for (int i = 0; i < 3; i++) drawNumberButton(BTN_OPT[i], q.options[i]);
}

void drawTenFrameQ(int idx, uint16_t bg) {
  TenFrameQ& q = tenFrameBank[idx];
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextFont(4);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("How many dots?", SCREEN_W / 2, scaleH(50));

  const int cell  = scaleMin(50);
  const int gridX = (SCREEN_W - 5 * cell) / 2;
  const int gridY = scaleH(130);
  // Outline
  tft.drawRect(gridX, gridY, 5 * cell, 2 * cell, TFT_WHITE);
  for (int i = 1; i < 5; i++)
    tft.drawFastVLine(gridX + i * cell, gridY, 2 * cell, TFT_WHITE);
  tft.drawFastHLine(gridX, gridY + cell, 5 * cell, TFT_WHITE);

  const int dotR = scaleMin(16);
  for (int row = 0; row < 2; row++) {
    for (int col = 0; col < 5; col++) {
      int n = row * 5 + col;
      int cx = gridX + col * cell + cell / 2;
      int cy = gridY + row * cell + cell / 2;
      if (n < q.filled) tft.fillCircle(cx, cy, dotR, TFT_YELLOW);
      else              tft.drawCircle(cx, cy, dotR, TFT_LIGHTGREY);
    }
  }
  for (int i = 0; i < 3; i++) drawNumberButton(BTN_OPT[i], q.options[i]);
}

void drawAddQ(int idx, uint16_t bg) {
  AddQ& q = addBank[idx];
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextFont(4);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Add it up", SCREEN_W / 2, scaleH(50));

  tft.setTextColor(TFT_YELLOW, bg);
  char eq[20];
  snprintf(eq, sizeof(eq), "%d + %d = ?", q.left, q.right);
  useBigFont();
  tft.drawString(eq, SCREEN_W / 2, scaleH(170));
  useDefaultFont();

  for (int i = 0; i < 3; i++) drawNumberButton(BTN_OPT[i], q.options[i]);
}

void drawMake10Q(int idx, uint16_t bg) {
  Make10Q& q = make10Bank[idx];
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextFont(4);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Make 10", SCREEN_W / 2, scaleH(50));

  tft.setTextColor(TFT_YELLOW, bg);
  char eq[20];
  snprintf(eq, sizeof(eq), "%d + ? = 10", q.left);
  useBigFont();
  tft.drawString(eq, SCREEN_W / 2, scaleH(170));
  useDefaultFont();

  for (int i = 0; i < 3; i++) drawNumberButton(BTN_OPT[i], q.options[i]);
}

void drawStartsWithQ(int idx, uint16_t bg) {
  StartsWithQ& q = startsWithBank[idx];
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextFont(4);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Which word starts with", SCREEN_W / 2, scaleH(55));

  tft.setTextColor(TFT_YELLOW, bg);
  char ltr[2] = { q.targetLetter, 0 };
  useBigFont();
  tft.drawString(ltr, SCREEN_W / 2, scaleH(125));
  useDefaultFont();

  for (int i = 0; i < 3; i++) drawWordButton(BTN_OPT[i], q.options[i]);
}

void drawMissingLetterQ(int idx, uint16_t bg) {
  MissingLetterQ& q = missingLetterBank[idx];
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextFont(4);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Find the missing letter", SCREEN_W / 2, scaleH(50));

  const int charSpacing = scaleW(60);
  const int xCenter = SCREEN_W / 2;
  const int yMid = scaleH(180);
  tft.setTextDatum(MC_DATUM);
  useBigFont();
  for (int i = 0; i < 3; i++) {
    char c = q.shown[i];
    int xi = xCenter + (i - 1) * charSpacing;
    if (c == '_') {
      tft.fillRect(xi - scaleW(20), yMid + scaleH(20), scaleW(40), scaleH(6), TFT_YELLOW);
    } else {
      char buf[2] = { c, 0 };
      tft.setTextColor(TFT_WHITE, bg);
      tft.drawString(buf, xi, yMid);
    }
  }
  useDefaultFont();

  for (int i = 0; i < 3; i++) drawCharButton(BTN_OPT[i], q.options[i]);
}

void drawRhymeQ(int idx, uint16_t bg) {
  RhymeQ& q = rhymeBank[idx];
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextFont(4);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Rhymes with", SCREEN_W / 2, scaleH(55));

  tft.setTextColor(TFT_YELLOW, bg);
  useBigFont();
  tft.drawString(q.target, SCREEN_W / 2, scaleH(130));
  useDefaultFont();

  for (int i = 0; i < 3; i++) drawWordButton(BTN_OPT[i], q.options[i]);
}

void drawUpperLowerQ(int idx, uint16_t bg) {
  UpperLowerQ& q = upperLowerBank[idx];
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextFont(4);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Match this letter", SCREEN_W / 2, scaleH(55));

  tft.setTextColor(TFT_YELLOW, bg);
  char buf[2] = { q.shown, 0 };
  useBigFont();
  tft.drawString(buf, SCREEN_W / 2, scaleH(125));
  useDefaultFont();

  for (int i = 0; i < 3; i++) drawCharButton(BTN_OPT[i], q.options[i]);
}

void drawCurrentQuestion() {
  RoundEntry& e = currentRound[currentQuestionIndex];
  uint16_t bg = (currentGameMode == GameMode::MATH) ? TFT_DARKCYAN : TFT_DARKGREEN;
  tft.fillScreen(bg);
  drawHeader(currentGameMode == GameMode::MATH ? "Math" : "Reading");

  switch (e.type) {
    case QType::COUNT:           drawCountQ(e.idx, bg);          break;
    case QType::MISSING_NUMBER:  drawMissingNumQ(e.idx, bg);     break;
    case QType::TEN_FRAME:       drawTenFrameQ(e.idx, bg);       break;
    case QType::ADD_UNDER_10:    drawAddQ(e.idx, bg);            break;
    case QType::MAKE_TEN:        drawMake10Q(e.idx, bg);         break;
    case QType::STARTS_WITH:     drawStartsWithQ(e.idx, bg);     break;
    case QType::MISSING_LETTER:  drawMissingLetterQ(e.idx, bg);  break;
    case QType::RHYME:           drawRhymeQ(e.idx, bg);          break;
    case QType::UPPER_LOWER:     drawUpperLowerQ(e.idx, bg);     break;
  }
  drawQuestionFooter(bg);
}

// ---------- Top-level screens ----------

void drawHome() {
  const uint16_t bg = TFT_PURPLE;
  drawConfettiBackground(bg, true);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_YELLOW, bg);
  useBigFont();
  tft.drawString("Dhruv", SCREEN_W / 2, scaleH(40));
  useDefaultFont();
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextFont(4);
  tft.drawString("Learning Arcade", SCREEN_W / 2, scaleH(110));

  drawButton(BTN_MATH);
  drawButton(BTN_READING);

  tft.setTextDatum(BC_DATUM);
  tft.setTextFont(4);
  tft.setTextColor(TFT_GOLD, bg);
  char buf[32];
  snprintf(buf, sizeof(buf), "Stars: %d", totalStars);
  tft.drawString(buf, SCREEN_W / 2, SCREEN_H - scaleH(25));
}

void drawFeedback() {
  uint16_t bg = lastAnswerCorrect ? TFT_DARKGREEN : TFT_PURPLE;
  tft.fillScreen(bg);
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextDatum(TC_DATUM);
  if (lastAnswerCorrect) {
    useBigFont();
    tft.drawString("Correct!", SCREEN_W / 2, scaleH(50));
    useDefaultFont();
    drawShape(Shape::STAR, SCREEN_W / 2, scaleH(220), scaleMin(60), bg);
    tft.setTextFont(4);
    tft.drawString("You got a star!", SCREEN_W / 2, scaleH(300));
    drawButton(BTN_NEXT);
  } else {
    useBigFont();
    tft.drawString("Try Again", SCREEN_W / 2, scaleH(50));
    useDefaultFont();
    tft.setTextFont(4);
    tft.drawString("You can do it!", SCREEN_W / 2, scaleH(220));
    drawButton(BTN_TRY);
  }
}

void drawRoundComplete() {
  const uint16_t bg = TFT_PURPLE;
  drawConfettiBackground(bg, false);
  tft.setTextColor(TFT_YELLOW, bg);
  tft.setTextDatum(TC_DATUM);
  useBigFont();
  tft.drawString("Great Job!", SCREEN_W / 2, scaleH(30));
  useDefaultFont();
  tft.setTextFont(4);

  // Stars reveal one by one — celebration beat. drawRoundComplete is only
  // called once per round entry (no re-redraws happen on this screen until
  // a navigation tap), so the animation only plays on the fresh transition.
  if (starsThisRound > 0) {
    const int starSpacing = scaleW(50);
    const int starR       = scaleMin(20);
    int sw = starsThisRound * starSpacing;
    int sx = (SCREEN_W - sw) / 2 + starSpacing / 2;
    for (int i = 0; i < starsThisRound; i++) {
      drawShape(Shape::STAR, sx + i * starSpacing, scaleH(170), starR, bg);
      delay(180);
    }
  }

  tft.setTextColor(TFT_WHITE, bg);
  char buf[40];
  snprintf(buf, sizeof(buf), "%d out of %d", correctThisRound, QUESTIONS_PER_ROUND);
  tft.drawString(buf, SCREEN_W / 2, scaleH(240));
  tft.setTextColor(TFT_GOLD, bg);
  snprintf(buf, sizeof(buf), "Total: %d stars", totalStars);
  tft.drawString(buf, SCREEN_W / 2, scaleH(290));
  drawButton(BTN_PLAY);
  drawButton(BTN_GOHOME);
}

void drawPinRows() {
  for (int i = 0; i < 10; i++) drawButton(BTN_PIN_DIGIT[i]);
  drawButton(BTN_PIN_CLEAR);
  drawButton(BTN_PIN_OK);
}

void drawPinDots(int topY) {
  const int boxW    = scaleW(40);
  const int boxH    = scaleH(40);
  const int spacing = scaleW(50);
  const int x0      = (SCREEN_W - 4 * spacing) / 2 + (spacing - boxW) / 2;
  for (int i = 0; i < 4; i++) {
    int bx = x0 + i * spacing;
    tft.drawRect(bx, topY, boxW, boxH, TFT_WHITE);
    if (i < pinLen) tft.fillCircle(bx + boxW / 2, topY + boxH / 2, scaleMin(8), TFT_YELLOW);
    else            tft.fillRect(bx + 1, topY + 1, boxW - 2, boxH - 2, TFT_BLACK);
  }
}

void drawLockScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(4);
  tft.drawString("Device Locked", SCREEN_W / 2, scaleH(20));
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextFont(2);
  tft.drawString("Enter PIN to unlock", SCREEN_W / 2, scaleH(57));
  drawPinDots(scaleH(82));
  if (pinWrong) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextFont(2);
    tft.drawString("Wrong PIN", SCREEN_W / 2, scaleH(132));
  }
  drawPinRows();
}

void drawAdmin() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  useBigFont();
  tft.drawString("Admin", SCREEN_W / 2, scaleH(20));
  useDefaultFont();
  tft.setTextFont(4);
  tft.setTextColor(TFT_GOLD, TFT_BLACK);
  char buf[40];
  snprintf(buf, sizeof(buf), "Stars: %d", totalStars);
  tft.drawString(buf, SCREEN_W / 2, scaleH(75));

  drawButton(BTN_RESET);

  char lblLock[16], lblMath[18], lblRead[20];
  snprintf(lblLock, sizeof(lblLock), "Lock: %s",    lockEnabled      ? "ON"   : "OFF");
  snprintf(lblMath, sizeof(lblMath), "Math: %s",    mathDifficulty   ? "Hard" : "Easy");
  snprintf(lblRead, sizeof(lblRead), "Reading: %s", readDifficulty   ? "Hard" : "Easy");
  drawWordButton(BTN_LOCK_TOGGLE, lblLock);
  drawWordButton(BTN_MATH_DIFF,   lblMath);
  drawWordButton(BTN_READ_DIFF,   lblRead);

  drawButton(BTN_CANCEL);
}

void drawPinEntry() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextFont(4);
  tft.drawString("Enter PIN", SCREEN_W / 2, scaleH(20));
  drawPinDots(scaleH(70));
  if (pinWrong) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextFont(2);
    tft.drawString("Wrong PIN", SCREEN_W / 2, scaleH(120));
  }
  drawButton(BTN_PIN_CANCEL);
  drawPinRows();
}

// ---------- Screensaver ----------

static const uint16_t SS_COLORS[] = {
  TFT_YELLOW, TFT_CYAN, TFT_MAGENTA, TFT_GREEN, TFT_ORANGE,
  TFT_WHITE,  TFT_GOLD, 0xFB8C,      0x07FF,    0xF81F,
};

void initScreensaver() {
  const int NC = sizeof(SS_COLORS) / sizeof(SS_COLORS[0]);
  for (int i = 0; i < NUM_PARTICLES; i++) {
    int8_t dx, dy;
    do {
      dx = (int8_t)((int)(esp_random() % 5) - 2);
      dy = (int8_t)((int)(esp_random() % 5) - 2);
    } while (dx == 0 && dy == 0);
    int16_t x = (int16_t)(esp_random() % SCREEN_W);
    int16_t y = (int16_t)(esp_random() % SCREEN_H);
    particles[i] = {x, y, x, y, dx, dy,
                    SS_COLORS[esp_random() % NC],
                    (uint8_t)(3 + esp_random() % 5)};
  }
  tft.fillScreen(TFT_BLACK);
  for (int i = 0; i < NUM_PARTICLES; i++)
    tft.fillCircle(particles[i].x, particles[i].y, particles[i].size, particles[i].color);
}

void tickScreensaver() {
  for (int i = 0; i < NUM_PARTICLES; i++) {
    tft.fillCircle(particles[i].px, particles[i].py, particles[i].size, TFT_BLACK);
    particles[i].px = particles[i].x;
    particles[i].py = particles[i].y;
    int nx = (int)particles[i].x + (int)particles[i].dx;
    int ny = (int)particles[i].y + (int)particles[i].dy;
    if (nx < 0) nx += SCREEN_W; else if (nx >= SCREEN_W) nx -= SCREEN_W;
    if (ny < 0) ny += SCREEN_H; else if (ny >= SCREEN_H) ny -= SCREEN_H;
    particles[i].x = (int16_t)nx;
    particles[i].y = (int16_t)ny;
    tft.fillCircle(particles[i].x, particles[i].y, particles[i].size, particles[i].color);
  }
}

void redraw() {
  switch (currentScreen) {
    case Screen::HOME:           drawHome();              break;
    case Screen::QUESTION:       drawCurrentQuestion();   break;
    case Screen::FEEDBACK:       drawFeedback();          break;
    case Screen::ROUND_COMPLETE: drawRoundComplete();     break;
    case Screen::PIN_ENTRY:      drawPinEntry();          break;
    case Screen::ADMIN:          drawAdmin();             break;
    case Screen::LOCK:           drawLockScreen();        break;
    case Screen::SCREENSAVER:                             break;
  }
  needsRedraw = false;
}

// ---------- Round / answer logic ----------

void saveStars() {
  prefs.begin("learning", false);
  prefs.putInt("stars", totalStars);
  prefs.end();
}

void saveSettings() {
  prefs.begin("learning", false);
  prefs.putBool("lock",     lockEnabled);
  prefs.putInt("mathDiff",  mathDifficulty);
  prefs.putInt("readDiff",  readDifficulty);
  prefs.end();
}

void loadSettings() {
  prefs.begin("learning", true);
  lockEnabled    = prefs.getBool("lock",    false);
  mathDifficulty = prefs.getInt("mathDiff", 1);
  readDifficulty = prefs.getInt("readDiff", 1);
  prefs.end();
}

void buildRound(const QType* types, int nTypes) {
  for (int i = 0; i < QUESTIONS_PER_ROUND; i++) {
    QType t = types[esp_random() % nTypes];
    int sz = bankSizeFor(t);
    int idx = sz > 0 ? (esp_random() % sz) : 0;
    currentRound[i] = { t, idx };
  }
}

void startMathRound() {
  static const QType MATH_EASY[] = {
    QType::COUNT, QType::TEN_FRAME, QType::ADD_UNDER_10
  };
  static const QType MATH_HARD[] = {
    QType::COUNT, QType::MISSING_NUMBER, QType::TEN_FRAME,
    QType::ADD_UNDER_10, QType::MAKE_TEN
  };
  currentGameMode      = GameMode::MATH;
  currentQuestionIndex = 0;
  correctThisRound     = 0;
  starsThisRound       = 0;
  if (mathDifficulty == 0) buildRound(MATH_EASY, sizeof(MATH_EASY) / sizeof(MATH_EASY[0]));
  else                     buildRound(MATH_HARD, sizeof(MATH_HARD) / sizeof(MATH_HARD[0]));
  currentScreen = Screen::QUESTION;
  needsRedraw   = true;
}

void startReadingRound() {
  static const QType READ_EASY[] = {
    QType::STARTS_WITH, QType::UPPER_LOWER
  };
  static const QType READ_HARD[] = {
    QType::STARTS_WITH, QType::MISSING_LETTER, QType::RHYME, QType::UPPER_LOWER
  };
  currentGameMode      = GameMode::READING;
  currentQuestionIndex = 0;
  correctThisRound     = 0;
  starsThisRound       = 0;
  if (readDifficulty == 0) buildRound(READ_EASY, sizeof(READ_EASY) / sizeof(READ_EASY[0]));
  else                     buildRound(READ_HARD, sizeof(READ_HARD) / sizeof(READ_HARD[0]));
  currentScreen = Screen::QUESTION;
  needsRedraw   = true;
}

bool checkAnswer(const RoundEntry& e, int optIdx) {
  switch (e.type) {
    case QType::COUNT:
      return countBank[e.idx].options[optIdx] == countBank[e.idx].correct;
    case QType::MISSING_NUMBER:
      return missingNumBank[e.idx].options[optIdx] == missingNumBank[e.idx].correctValue;
    case QType::TEN_FRAME:
      return tenFrameBank[e.idx].options[optIdx] == tenFrameBank[e.idx].correct;
    case QType::ADD_UNDER_10:
      return addBank[e.idx].options[optIdx] == addBank[e.idx].correct;
    case QType::MAKE_TEN:
      return make10Bank[e.idx].options[optIdx] == make10Bank[e.idx].correct;
    case QType::STARTS_WITH:
      return optIdx == startsWithBank[e.idx].correctIdx;
    case QType::MISSING_LETTER:
      return optIdx == missingLetterBank[e.idx].correctIdx;
    case QType::RHYME:
      return optIdx == rhymeBank[e.idx].correctIdx;
    case QType::UPPER_LOWER:
      return optIdx == upperLowerBank[e.idx].correctIdx;
  }
  return false;
}

void recordAnswer(bool correct) {
  lastAnswerCorrect = correct;
  if (correct) {
    correctThisRound++;
    starsThisRound++;
    // totalStars is NOT updated here. Stars are credited only when the kid
    // finishes all 5 questions — see advanceAfterFeedback. Mid-round exits
    // forfeit the round's stars.
  }
  currentScreen = Screen::FEEDBACK;
  needsRedraw   = true;
}

void handleAnswer(int optIdx) {
  recordAnswer(checkAnswer(currentRound[currentQuestionIndex], optIdx));
}

void advanceAfterFeedback() {
  if (lastAnswerCorrect) {
    currentQuestionIndex++;
    if (currentQuestionIndex >= QUESTIONS_PER_ROUND) {
      // Round finished — credit the round's stars to the lifetime total
      // and persist. This is the only place that touches totalStars/saveStars.
      totalStars += starsThisRound;
      saveStars();
      currentScreen = Screen::ROUND_COMPLETE;
    } else {
      currentScreen = Screen::QUESTION;
    }
  } else {
    currentScreen = Screen::QUESTION;
  }
  needsRedraw = true;
}

void handleTap(int16_t sx, int16_t sy) {
  switch (currentScreen) {
    case Screen::HOME:
      if      (tapInside(BTN_MATH,    sx, sy)) { flashButton(BTN_MATH);    startMathRound();    }
      else if (tapInside(BTN_READING, sx, sy)) { flashButton(BTN_READING); startReadingRound(); }
      break;
    case Screen::QUESTION:
      if (tapInside(BTN_EXIT_GAME, sx, sy)) {
        // Bail out — forfeit this round's stars (they were never credited
        // to totalStars; starsThisRound just gets reset for the next round).
        starsThisRound       = 0;
        correctThisRound     = 0;
        currentQuestionIndex = 0;
        currentGameMode      = GameMode::NONE;
        currentScreen        = Screen::HOME;
        needsRedraw          = true;
        return;
      }
      for (int i = 0; i < 3; i++) {
        if (tapInside(BTN_OPT[i], sx, sy)) { flashButton(BTN_OPT[i]); handleAnswer(i); return; }
      }
      break;
    case Screen::FEEDBACK:
      if (lastAnswerCorrect) {
        if (tapInside(BTN_NEXT, sx, sy)) { flashButton(BTN_NEXT); advanceAfterFeedback(); }
      } else {
        if (tapInside(BTN_TRY,  sx, sy)) { flashButton(BTN_TRY);  advanceAfterFeedback(); }
      }
      break;
    case Screen::ROUND_COMPLETE:
      if (tapInside(BTN_PLAY, sx, sy)) {
        flashButton(BTN_PLAY);
        if (currentGameMode == GameMode::READING) startReadingRound();
        else                                      startMathRound();
      } else if (tapInside(BTN_GOHOME, sx, sy)) {
        flashButton(BTN_GOHOME);
        currentScreen = Screen::HOME;
        needsRedraw   = true;
      }
      break;
    case Screen::PIN_ENTRY:
      if (tapInside(BTN_PIN_CANCEL, sx, sy)) {
        flashButton(BTN_PIN_CANCEL);
        pinLen = 0; pinBuffer[0] = 0; pinWrong = false;
        currentScreen = Screen::HOME;
        needsRedraw   = true;
        return;
      }
      if (tapInside(BTN_PIN_OK, sx, sy)) {
        if (strcmp(pinBuffer, ADMIN_PIN) == 0) {
          pinLen = 0; pinBuffer[0] = 0; pinWrong = false;
          currentScreen = Screen::ADMIN;
          Serial.println("ADMIN: PIN ok → admin panel");
        } else {
          pinLen = 0; pinBuffer[0] = 0; pinWrong = true;
          Serial.println("ADMIN: PIN wrong");
        }
        needsRedraw = true;
        return;
      }
      if (tapInside(BTN_PIN_CLEAR, sx, sy)) {
        pinLen = 0; pinBuffer[0] = 0; pinWrong = false;
        needsRedraw = true;
        return;
      }
      for (int i = 0; i < 10; i++) {
        if (tapInside(BTN_PIN_DIGIT[i], sx, sy)) {
          if (pinLen < 4) {
            pinBuffer[pinLen++] = '0' + i;
            pinBuffer[pinLen]   = 0;
            pinWrong = false;
            needsRedraw = true;
          }
          return;
        }
      }
      break;
    case Screen::ADMIN:
      if (tapInside(BTN_RESET, sx, sy)) {
        flashButton(BTN_RESET);
        totalStars = 0;
        saveStars();
        currentScreen = Screen::HOME;
        needsRedraw   = true;
      } else if (tapInside(BTN_LOCK_TOGGLE, sx, sy)) {
        flashButton(BTN_LOCK_TOGGLE);
        lockEnabled = !lockEnabled;
        saveSettings();
        needsRedraw = true;
      } else if (tapInside(BTN_MATH_DIFF, sx, sy)) {
        flashButton(BTN_MATH_DIFF);
        mathDifficulty = !mathDifficulty;
        saveSettings();
        needsRedraw = true;
      } else if (tapInside(BTN_READ_DIFF, sx, sy)) {
        flashButton(BTN_READ_DIFF);
        readDifficulty = !readDifficulty;
        saveSettings();
        needsRedraw = true;
      } else if (tapInside(BTN_CANCEL, sx, sy)) {
        flashButton(BTN_CANCEL);
        currentScreen = Screen::HOME;
        needsRedraw   = true;
      }
      break;
    case Screen::LOCK:
      if (tapInside(BTN_PIN_OK, sx, sy)) {
        if (strcmp(pinBuffer, ADMIN_PIN) == 0) {
          pinLen = 0; pinBuffer[0] = 0; pinWrong = false;
          currentScreen = Screen::HOME;
        } else {
          pinLen = 0; pinBuffer[0] = 0; pinWrong = true;
        }
        needsRedraw = true;
        return;
      }
      if (tapInside(BTN_PIN_CLEAR, sx, sy)) {
        pinLen = 0; pinBuffer[0] = 0; pinWrong = false;
        needsRedraw = true;
        return;
      }
      for (int i = 0; i < 10; i++) {
        if (tapInside(BTN_PIN_DIGIT[i], sx, sy)) {
          if (pinLen < 4) {
            pinBuffer[pinLen++] = '0' + i;
            pinBuffer[pinLen]   = 0;
            pinWrong = false;
            needsRedraw = true;
          }
          return;
        }
      }
      break;
  }
}

// ---------- Bank init + SD loading ----------

Shape parseShape(const char* s) {
  if (!s) return Shape::BALL;
  if (strcasecmp(s, "apple")    == 0) return Shape::APPLE;
  if (strcasecmp(s, "star")     == 0) return Shape::STAR;
  if (strcasecmp(s, "flower")   == 0) return Shape::FLOWER;
  if (strcasecmp(s, "moon")     == 0) return Shape::MOON;
  if (strcasecmp(s, "heart")    == 0) return Shape::HEART;
  if (strcasecmp(s, "triangle") == 0) return Shape::TRIANGLE;
  if (strcasecmp(s, "square")   == 0) return Shape::SQUARE;
  return Shape::BALL;
}

// Copy hardcoded defaults into working banks (SD data appends after these).
void initBanks() {
  countBankSize = sizeof(COUNT_DEFAULT) / sizeof(COUNT_DEFAULT[0]);
  memcpy(countBank, COUNT_DEFAULT, countBankSize * sizeof(CountQ));

  missingNumBankSize = sizeof(MISNUM_DEFAULT) / sizeof(MISNUM_DEFAULT[0]);
  memcpy(missingNumBank, MISNUM_DEFAULT, missingNumBankSize * sizeof(MissingNumQ));

  addBankSize = sizeof(ADD_DEFAULT) / sizeof(ADD_DEFAULT[0]);
  memcpy(addBank, ADD_DEFAULT, addBankSize * sizeof(AddQ));

  make10BankSize = sizeof(MAKE10_DEFAULT) / sizeof(MAKE10_DEFAULT[0]);
  memcpy(make10Bank, MAKE10_DEFAULT, make10BankSize * sizeof(Make10Q));

  tenFrameBankSize = sizeof(TENFRAME_DEFAULT) / sizeof(TENFRAME_DEFAULT[0]);
  memcpy(tenFrameBank, TENFRAME_DEFAULT, tenFrameBankSize * sizeof(TenFrameQ));

  startsWithBankSize = sizeof(STARTSWITH_DEFAULT) / sizeof(STARTSWITH_DEFAULT[0]);
  memcpy(startsWithBank, STARTSWITH_DEFAULT, startsWithBankSize * sizeof(StartsWithQ));

  missingLetterBankSize = sizeof(MISSINGLETTER_DEFAULT) / sizeof(MISSINGLETTER_DEFAULT[0]);
  memcpy(missingLetterBank, MISSINGLETTER_DEFAULT, missingLetterBankSize * sizeof(MissingLetterQ));

  rhymeBankSize = sizeof(RHYME_DEFAULT) / sizeof(RHYME_DEFAULT[0]);
  memcpy(rhymeBank, RHYME_DEFAULT, rhymeBankSize * sizeof(RhymeQ));

  upperLowerBankSize = sizeof(UPPERLOWER_DEFAULT) / sizeof(UPPERLOWER_DEFAULT[0]);
  memcpy(upperLowerBank, UPPERLOWER_DEFAULT, upperLowerBankSize * sizeof(UpperLowerQ));
}

// Helper: insert correct answer at a random position among 3 slots.
static void shuffleIntoOpts(int correct, int w0, int w1, int* out3) {
  int pos = esp_random() % 3, wi = 0;
  for (int j = 0; j < 3; j++) out3[j] = (j == pos) ? correct : (j < pos ? (wi == 0 ? w0 : w1) : (wi == 0 ? w0 : w1));
  // Simpler, deterministic: just place correct at pos, wrongs fill the rest.
  wi = 0;
  int wrongs[2] = {w0, w1};
  for (int j = 0; j < 3; j++) out3[j] = (j == pos) ? correct : wrongs[wi++];
}

void loadCountFromSD() {
  File f = SD.open("/questions/math_count.json");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
  f.close();
  for (JsonObject q : doc.as<JsonArray>()) {
    if (countBankSize >= 64) break;
    const char* prompt = q["prompt"] | "How many?";
    int count = q["count"] | 0;
    JsonArray wrong = q["wrong"];
    if (!wrong || wrong.size() < 3 || !count) continue;
    int opts[3]; shuffleIntoOpts(count, wrong[0], wrong[1], opts); opts[2] = (opts[0]==count||opts[1]==count) ? (int)wrong[2] : opts[2];
    countBank[countBankSize++] = {poolStr(prompt), parseShape(q["shape"] | "ball"), count, {opts[0],opts[1],opts[2]}, count};
  }
  Serial.printf("SD: countBank now %d questions\n", countBankSize);
}

void loadMissingNumFromSD() {
  File f = SD.open("/questions/math_missing_num.json");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
  f.close();
  for (JsonObject q : doc.as<JsonArray>()) {
    if (missingNumBankSize >= 32) break;
    JsonArray seq = q["seq"];
    JsonArray opts = q["options"];
    int correct = q["correct"] | 0;
    if (!seq || seq.size() != 4 || !opts || opts.size() < 3) continue;
    missingNumBank[missingNumBankSize++] = {
      {(int)seq[0],(int)seq[1],(int)seq[2],(int)seq[3]},
      {(int)opts[0],(int)opts[1],(int)opts[2]}, correct
    };
  }
  Serial.printf("SD: missingNumBank now %d questions\n", missingNumBankSize);
}

void loadAddFromSD() {
  File f = SD.open("/questions/math_add.json");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
  f.close();
  for (JsonObject q : doc.as<JsonArray>()) {
    if (addBankSize >= 64) break;
    int a = q["a"] | 0, b = q["b"] | 0;
    JsonArray wrong = q["wrong"];
    if (!wrong || wrong.size() < 3) continue;
    int correct = a + b;
    int opts[3]; shuffleIntoOpts(correct, wrong[0], wrong[1], opts); opts[2] = (int)wrong[2];
    addBank[addBankSize++] = {a, b, {opts[0],opts[1],opts[2]}, correct};
  }
  Serial.printf("SD: addBank now %d questions\n", addBankSize);
}

void loadMake10FromSD() {
  File f = SD.open("/questions/math_make10.json");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
  f.close();
  for (JsonObject q : doc.as<JsonArray>()) {
    if (make10BankSize >= 64) break;
    int given = q["given"] | 0;
    JsonArray wrong = q["wrong"];
    if (!wrong || wrong.size() < 3) continue;
    int correct = 10 - given;
    int opts[3]; shuffleIntoOpts(correct, wrong[0], wrong[1], opts); opts[2] = (int)wrong[2];
    make10Bank[make10BankSize++] = {given, {opts[0],opts[1],opts[2]}, correct};
  }
  Serial.printf("SD: make10Bank now %d questions\n", make10BankSize);
}

void loadTenFrameFromSD() {
  File f = SD.open("/questions/math_ten_frame.json");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
  f.close();
  for (JsonObject q : doc.as<JsonArray>()) {
    if (tenFrameBankSize >= 32) break;
    int filled = q["filled"] | 0;
    JsonArray wrong = q["wrong"];
    if (!wrong || wrong.size() < 3) continue;
    int opts[3]; shuffleIntoOpts(filled, wrong[0], wrong[1], opts); opts[2] = (int)wrong[2];
    tenFrameBank[tenFrameBankSize++] = {filled, {opts[0],opts[1],opts[2]}, filled};
  }
  Serial.printf("SD: tenFrameBank now %d questions\n", tenFrameBankSize);
}

void loadStartsWithFromSD() {
  File f = SD.open("/questions/reading_starts_with.json");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
  f.close();
  for (JsonObject q : doc.as<JsonArray>()) {
    if (startsWithBankSize >= 64) break;
    const char* letter = q["letter"] | "A";
    JsonArray opts = q["options"];
    int correct = q["correct"] | 0;
    if (!opts || opts.size() < 3) continue;
    startsWithBank[startsWithBankSize++] = {
      letter[0],
      {poolStr(opts[0]|""), poolStr(opts[1]|""), poolStr(opts[2]|"")},
      correct
    };
  }
  Serial.printf("SD: startsWithBank now %d questions\n", startsWithBankSize);
}

void loadMissingLetterFromSD() {
  File f = SD.open("/questions/reading_missing_letter.json");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
  f.close();
  for (JsonObject q : doc.as<JsonArray>()) {
    if (missingLetterBankSize >= 64) break;
    const char* word = q["word"] | "";
    JsonArray opts = q["options"];
    int correct = q["correct"] | 0;
    if (!opts || opts.size() < 3 || !word[0]) continue;
    const char* o0 = opts[0]|"A"; const char* o1 = opts[1]|"B"; const char* o2 = opts[2]|"C";
    missingLetterBank[missingLetterBankSize++] = {poolStr(word), {o0[0],o1[0],o2[0]}, correct};
  }
  Serial.printf("SD: missingLetterBank now %d questions\n", missingLetterBankSize);
}

void loadRhymeFromSD() {
  File f = SD.open("/questions/reading_rhyme.json");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
  f.close();
  for (JsonObject q : doc.as<JsonArray>()) {
    if (rhymeBankSize >= 64) break;
    const char* target = q["target"] | "";
    JsonArray opts = q["options"];
    int correct = q["correct"] | 0;
    if (!opts || opts.size() < 3 || !target[0]) continue;
    rhymeBank[rhymeBankSize++] = {
      poolStr(target),
      {poolStr(opts[0]|""), poolStr(opts[1]|""), poolStr(opts[2]|"")},
      correct
    };
  }
  Serial.printf("SD: rhymeBank now %d questions\n", rhymeBankSize);
}

void loadUpperLowerFromSD() {
  File f = SD.open("/questions/reading_upper_lower.json");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
  f.close();
  for (JsonObject q : doc.as<JsonArray>()) {
    if (upperLowerBankSize >= 64) break;
    const char* shown = q["shown"] | "a";
    JsonArray opts = q["options"];
    int correct = q["correct"] | 0;
    if (!opts || opts.size() < 3) continue;
    const char* o0=opts[0]|"A"; const char* o1=opts[1]|"B"; const char* o2=opts[2]|"C";
    upperLowerBank[upperLowerBankSize++] = {shown[0], {o0[0],o1[0],o2[0]}, correct};
  }
  Serial.printf("SD: upperLowerBank now %d questions\n", upperLowerBankSize);
}

void loadAllBanksFromSD() {
  SPIClass sdSpi(HSPI);
  sdSpi.begin(18, 19, 23, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, sdSpi)) {
    Serial.println("SD: no card — using hardcoded banks");
    return;
  }
  Serial.println("SD: card mounted, loading question banks");
  loadCountFromSD();
  loadMissingNumFromSD();
  loadAddFromSD();
  loadMake10FromSD();
  loadTenFrameFromSD();
  loadStartsWithFromSD();
  loadMissingLetterFromSD();
  loadRhymeFromSD();
  loadUpperLowerFromSD();
  SD.end();
}

// ---------- Setup / loop ----------

void setup() {
  Serial.begin(115200);
  delay(300);

  tft.init();
  tft.setRotation(0);

  initBanks();
  loadAllBanksFromSD();

  prefs.begin("learning", true);
  totalStars = prefs.getInt("stars", 0);
  prefs.end();

  loadSettings();
  if (lockEnabled) currentScreen = Screen::LOCK;

  lastInteractionMs = millis();
  Serial.printf("Boot. totalStars=%d lock=%d\n", totalStars, lockEnabled);
}

void loop() {
  if (currentScreen != Screen::SCREENSAVER &&
      millis() - lastInteractionMs > SCREENSAVER_TIMEOUT_MS) {
    currentScreen = Screen::SCREENSAVER;
    initScreensaver();
  }

  if (currentScreen == Screen::SCREENSAVER &&
      millis() - lastScreensaverTickMs > SCREENSAVER_FRAME_MS) {
    tickScreensaver();
    lastScreensaverTickMs = millis();
  }

  if (needsRedraw) redraw();

  uint16_t rx, ry;
  bool isTouched = tft.getTouchRaw(&rx, &ry) && tft.getTouchRawZ() > TOUCH_Z_THRESHOLD;

  if (isTouched && !wasTouched) {
    lastInteractionMs = millis();
    if (currentScreen == Screen::SCREENSAVER) {
      pinLen = 0; pinBuffer[0] = 0; pinWrong = false;
      currentScreen = lockEnabled ? Screen::LOCK : Screen::HOME;
      needsRedraw   = true;
      wasTouched    = isTouched;
      return;
    }
    int16_t sx = mapTouchX(rx);
    int16_t sy = mapTouchY(ry);
    touchStartMs   = millis();
    adminHoldArmed = (currentScreen == Screen::HOME &&
                      sx >= ADMIN_ZONE_X && sy >= ADMIN_ZONE_Y);
    Serial.printf("TAP raw=(%u,%u) mapped=(%d,%d) screen=%d admin=%d\n", rx, ry, sx, sy, (int)currentScreen, adminHoldArmed);
    handleTap(sx, sy);
  } else if (isTouched && wasTouched) {
    if (adminHoldArmed && (millis() - touchStartMs > ADMIN_HOLD_MS)) {
      pinLen = 0; pinBuffer[0] = 0; pinWrong = false;
      currentScreen  = Screen::PIN_ENTRY;
      needsRedraw    = true;
      adminHoldArmed = false;
      Serial.println("ADMIN HOLD: triggered → PIN entry");
    }
  } else if (!isTouched) {
    if (adminHoldArmed) {
      Serial.printf("ADMIN HOLD: lost at %lums (need %lums)\n",
                    millis() - touchStartMs, ADMIN_HOLD_MS);
    }
    adminHoldArmed = false;
  }
  wasTouched = isTouched;
  delay(15);
}
