/*
 * ╔══════════════════════════════════════════════════════════════╗
 *   CLAWD MOCHI — ESP32-C3 Super Mini + ST7789 1.54" 240×240
 *
 *   Wiring:
 *     SDA → GPIO 9  (hardware SPI MOSI)
 *     SCL → GPIO 8   (hardware SPI SCK)
 *     RST → GPIO 2
 *     DC  → GPIO 1
 *     CS  → GPIO 4
 *     BL  → GPIO 3
 *     VCC → 3V3
 *     GND → GND
 *
 *   AP:  "ClaWD-Mochi"  pw: clawd1234  → http://192.168.4.1
 *   STA: configure via web portal (saved to flash)
 *   Monitor: GET http://<sta-ip>/status?s=idle|thinking|working|done|alert|offline
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <math.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// ── Pins ──────────────────────────────────────────────────────
#define TFT_CS  4
#define TFT_DC  1
#define TFT_RST 2
#define TFT_BLK 3

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// ── WiFi ──────────────────────────────────────────────────────
const char* AP_SSID = "ClaWD-Mochi";
const char* AP_PASS = "clawd1234";
WebServer server(80);
Preferences prefs;

bool     staConnected = false;
String   staIP        = "";
String   savedSSID    = "";
String   savedPASS    = "";

#define STA_CONNECT_TIMEOUT_MS 10000
#define STATUS_TIMEOUT_MS      30000

// ── Display ───────────────────────────────────────────────────
#define DISP_W 240
#define DISP_H 240

// ── Eye constants (shared by both eye views) ──────────────────
#define EYE_W   30
#define EYE_H   60
#define EYE_GAP 120
#define EYE_OX  0     // horizontal offset
#define EYE_OY  40    // vertical offset upward (subtracted from centre)

// ── Colours ───────────────────────────────────────────────────
uint16_t C_ORANGE, C_DARKBG, C_MUTED, C_GREEN;
#define C_WHITE ST77XX_WHITE
#define C_BLACK ST77XX_BLACK

// ── State ─────────────────────────────────────────────────────
#define VIEW_EYES_NORMAL 0
#define VIEW_EYES_SQUISH 1
#define VIEW_CODE        2
#define VIEW_DRAW        3
#define VIEW_MONITOR     4

#define MON_IDLE     0
#define MON_THINKING 1
#define MON_WORKING  2
#define MON_ALERT    3
#define MON_OFFLINE  4

#define IDLE_NORMAL  0
#define IDLE_SLEEPY  1
#define IDLE_HEART   2
#define IDLE_HAPPY   3

#define SCAN_EYE_W   20
#define IDLE_SWITCH_MIN_MS 15000
#define IDLE_SWITCH_MAX_MS 45000

const uint8_t IDLE_POOL[] = { IDLE_NORMAL, IDLE_SLEEPY, IDLE_HEART, IDLE_HAPPY };
#define IDLE_POOL_SIZE 4

// ── Working sub-acts (semantic working states) ───────────────
#define ACT_WORK  0
#define ACT_READ  1
#define ACT_EDIT  2
#define ACT_RUN   3
#define ACT_NET   4
#define ACT_AGENT 5

uint8_t workAct = ACT_WORK;
char    tickerText[32] = "";

uint8_t  currentView   = VIEW_EYES_NORMAL;
uint8_t  monitorState  = MON_IDLE;
uint8_t  currentIdleIndex = 0;
uint8_t  currentIdleExpr  = IDLE_NORMAL;
uint8_t  animPhase        = 0;
unsigned long lastStatusMs = 0;
unsigned long lastIdleSwitch = 0;
unsigned long lastAnimTick   = 0;
uint32_t nextIdleSwitchMs = 30000;

#define EXPR_ZONE_Y  28
#define EXPR_ZONE_H  185

struct EyeRect { int16_t x, y, w, h; bool valid; };
EyeRect prevEyeL = {0, 0, 0, 0, false};
EyeRect prevEyeR = {0, 0, 0, 0, false};
uint8_t lastRenderKey = 255;
int16_t lastTickOx    = -999;
bool    lastTickBlink = false;
int16_t lastTickScanOx = -999;
uint8_t lastTickSquishState = 255;
uint8_t lastTickHeartScale   = 255;
int16_t lastSleepyTop        = -999;
uint8_t lastSleepyZ          = 255;
bool     statusTimedOut  = false;
bool     busy         = false;
bool     backlightOn  = true;
uint8_t  animSpeed    = 2;   // 1=slow 2=normal(default) 3=fast

uint16_t animBgColor  = 0;   // background for eye/logo animations
uint16_t drawBgColor  = 0;   // background for canvas

// ── Eye rig: lively expression engine ────────────────────────
enum EyeStyle : uint8_t { STYLE_RECT, STYLE_CHEVRON, STYLE_ARC, STYLE_HEART };

struct EyePose {
  EyeStyle style;
  int16_t  ox, oy;   // 眼对偏移
  int16_t  w, h;     // RECT:眼宽高 | CHEVRON:w=reach,h=2*arm | ARC:w=弧宽 | HEART:w=像素 scale
  uint8_t  lid;      // 眼睑 0=全开 .. 240=全闭
};

#define RIG_BLINK    0x01
#define RIG_SACCADE  0x02
#define RIG_BREATH   0x04
#define RIG_BREATH2  0x08   // 双倍呼吸幅度(困倦)

struct Spring { int32_t cur, vel; };   // 8.8 定点

struct EyeRig {
  EyePose  pose;            // 目标姿态
  uint8_t  flags;
  EyeStyle drawnStyle;      // 当前屏上样式
  Spring   ox, oy, w, h, lid;
  unsigned long nextBlinkMs, nextSaccadeMs;
  uint8_t  blinkFrame;      // 0=未眨眼,1..BLINK_FRAMES=进行中
  bool     blinkAgain;      // 双连眨待续
  int16_t  sacX;
  uint16_t breathPhase;     // 8.8 相位
  EyeRect  prevL, prevR;
  bool     prevValid;
  bool     zoneDirty;       // 需整区清屏(样式切换等)
  uint8_t  trans;           // 样式过渡 0=无 1=闭眼中 2=睁眼中
  EyePose  transNext;
  uint8_t  transFlagsNext;
};

EyeRig rig;
bool rigZoneCleared = false;   // 本帧是否清过表情区(供覆盖层重绘判断)

unsigned long rigBehNextMs = 0;   // 行为脚本计时(表情切换时复位)
uint8_t       rigBehStep   = 0;

#define RIG_TICK_MS  33
#define RIG_DAMP     196       // 速度阻尼(/256)
#define BLINK_FRAMES 7
static const uint8_t BLINK_H_PCT[BLINK_FRAMES] = {106, 60, 8, 8, 70, 104, 100};
static const uint8_t BLINK_W_PCT[BLINK_FRAMES] = { 97, 105, 118, 118, 103, 98, 100};
static const int8_t  BREATH_TAB[16] = {0,1,1,2,2,2,1,1,0,-1,-1,-2,-2,-2,-1,-1};

// 姿态预设
const EyePose POSE_NORMAL = {STYLE_RECT,    0,  0, EYE_W, EYE_H, 0};
const EyePose POSE_SLEEPY = {STYLE_RECT,    0,  8, EYE_W, EYE_H, 170};
const EyePose POSE_HEART  = {STYLE_HEART,   0,  0, 6, 6, 0};
const EyePose POSE_HAPPY  = {STYLE_ARC,     0,  8, 30, 30, 0};
const EyePose POSE_THINK  = {STYLE_CHEVRON, 0,  0, EYE_W / 2, EYE_H, 0};
const EyePose POSE_SCAN   = {STYLE_RECT,    0,  0, SCAN_EYE_W, EYE_H, 0};
const EyePose POSE_READ   = {STYLE_RECT,    0, 26, 26, 16, 0};
const EyePose POSE_EDIT   = {STYLE_RECT,    0,  0, 22, 38, 50};
const EyePose POSE_RUN    = {STYLE_RECT,    0,  0, 14, 44, 0};
const EyePose POSE_NET    = {STYLE_RECT,    0,  0, EYE_W, EYE_H, 0};

// ── Terminal ──────────────────────────────────────────────────
#define TERM_COLS      15
#define TERM_ROWS       8
#define TERM_CHAR_W    12
#define TERM_CHAR_H    20
#define TERM_PAD_X      8
#define TERM_PAD_Y     18

bool    termMode    = false;
String  termLines[TERM_ROWS];
uint8_t termRow     = 0;
uint8_t termCol     = 0;

// ── Logo data ─────────────────────────────────────────────────
#define LOGO_CX 120
#define LOGO_CY 105

#define LOGO_TRI_COUNT 162
static const int16_t LOGO_TRIS[][6] PROGMEM = {
  {120,105,65,134,100,114},{120,105,100,114,101,113},{120,105,101,113,100,112},
  {120,105,100,112,99,112},{120,105,99,112,93,111},{120,105,93,111,73,111},
  {120,105,73,111,55,110},{120,105,55,110,38,109},{120,105,38,109,34,108},
  {120,105,34,108,30,103},{120,105,30,103,30,100},{120,105,30,100,34,98},
  {120,105,34,98,39,98},{120,105,39,98,50,99},{120,105,50,99,67,100},
  {120,105,67,100,80,101},{120,105,80,101,98,103},{120,105,98,103,101,103},
  {120,105,101,103,101,102},{120,105,101,102,100,101},{120,105,100,101,100,100},
  {120,105,100,100,82,88},{120,105,82,88,63,76},{120,105,63,76,53,69},
  {120,105,53,69,48,65},{120,105,48,65,45,61},{120,105,45,61,44,54},
  {120,105,44,54,49,49},{120,105,49,49,55,49},{120,105,55,49,57,49},
  {120,105,57,49,64,55},{120,105,64,55,78,66},{120,105,78,66,96,79},
  {120,105,96,79,99,81},{120,105,99,81,100,81},{120,105,100,81,100,80},
  {120,105,100,80,99,78},{120,105,99,78,89,60},{120,105,89,60,78,41},
  {120,105,78,41,73,34},{120,105,73,34,72,29},{120,105,72,29,72,28},
  {120,105,72,28,72,27},{120,105,72,27,71,26},{120,105,71,26,71,25},
  {120,105,71,25,71,24},{120,105,71,24,77,16},{120,105,77,16,80,15},
  {120,105,80,15,87,16},{120,105,87,16,91,19},{120,105,91,19,95,29},
  {120,105,95,29,103,46},{120,105,103,46,114,68},{120,105,114,68,118,75},
  {120,105,118,75,119,81},{120,105,119,81,120,83},{120,105,120,83,121,83},
  {120,105,121,83,121,82},{120,105,121,82,122,69},{120,105,122,69,124,54},
  {120,105,124,54,126,34},{120,105,126,34,126,28},{120,105,126,28,129,21},
  {120,105,129,21,135,18},{120,105,135,18,139,20},{120,105,139,20,143,25},
  {120,105,143,25,142,28},{120,105,142,28,140,42},{120,105,140,42,136,64},
  {120,105,136,64,133,78},{120,105,133,78,135,78},{120,105,135,78,136,76},
  {120,105,136,76,144,67},{120,105,144,67,156,51},{120,105,156,51,162,45},
  {120,105,162,45,168,38},{120,105,168,38,172,35},{120,105,172,35,180,35},
  {120,105,180,35,185,43},{120,105,185,43,183,52},{120,105,183,52,175,62},
  {120,105,175,62,168,71},{120,105,168,71,159,83},{120,105,159,83,153,94},
  {120,105,153,94,154,94},{120,105,154,94,155,94},{120,105,155,94,176,90},
  {120,105,176,90,188,88},{120,105,188,88,201,85},{120,105,201,85,208,88},
  {120,105,208,88,208,91},{120,105,208,91,206,97},{120,105,206,97,191,101},
  {120,105,191,101,174,104},{120,105,174,104,148,110},{120,105,148,110,148,111},
  {120,105,148,111,148,111},{120,105,148,111,160,112},{120,105,160,112,165,112},
  {120,105,165,112,177,112},{120,105,177,112,200,114},{120,105,200,114,205,118},
  {120,105,205,118,209,123},{120,105,209,123,208,126},{120,105,208,126,199,131},
  {120,105,199,131,187,128},{120,105,187,128,159,121},{120,105,159,121,149,119},
  {120,105,149,119,147,119},{120,105,147,119,147,120},{120,105,147,120,156,128},
  {120,105,156,128,170,141},{120,105,170,141,189,158},{120,105,189,158,190,163},
  {120,105,190,163,188,166},{120,105,188,166,185,166},{120,105,185,166,169,153},
  {120,105,169,153,162,148},{120,105,162,148,148,136},{120,105,148,136,147,136},
  {120,105,147,136,147,137},{120,105,147,137,150,142},{120,105,150,142,168,168},
  {120,105,168,168,169,176},{120,105,169,176,168,179},{120,105,168,179,163,180},
  {120,105,163,180,158,179},{120,105,158,179,148,165},{120,105,148,165,137,149},
  {120,105,137,149,129,134},{120,105,129,134,128,135},{120,105,128,135,123,189},
  {120,105,123,189,120,192},{120,105,120,192,115,194},{120,105,115,194,110,191},
  {120,105,110,191,108,185},{120,105,108,185,110,174},{120,105,110,174,113,160},
  {120,105,113,160,116,148},{120,105,116,148,118,134},{120,105,118,134,119,129},
  {120,105,119,129,119,129},{120,105,119,129,118,129},{120,105,118,129,107,144},
  {120,105,107,144,91,166},{120,105,91,166,78,180},{120,105,78,180,75,181},
  {120,105,75,181,70,178},{120,105,70,178,70,173},{120,105,70,173,73,169},
  {120,105,73,169,91,146},{120,105,91,146,102,132},{120,105,102,132,109,124},
  {120,105,109,124,109,123},{120,105,109,123,108,123},{120,105,108,123,61,153},
  {120,105,61,153,52,155},{120,105,52,155,49,151},{120,105,49,151,49,146},
  {120,105,49,146,51,144},{120,105,51,144,65,134},{120,105,65,134,65,134},
};

#define LOGO_SEG_COUNT 162
static const int16_t LOGO_SEGS[][4] PROGMEM = {
  {65,134,100,114},{100,114,101,113},{101,113,100,112},{100,112,99,112},
  {99,112,93,111},{93,111,73,111},{73,111,55,110},{55,110,38,109},
  {38,109,34,108},{34,108,30,103},{30,103,30,100},{30,100,34,98},
  {34,98,39,98},{39,98,50,99},{50,99,67,100},{67,100,80,101},
  {80,101,98,103},{98,103,101,103},{101,103,101,102},{101,102,100,101},
  {100,101,100,100},{100,100,82,88},{82,88,63,76},{63,76,53,69},
  {53,69,48,65},{48,65,45,61},{45,61,44,54},{44,54,49,49},
  {49,49,55,49},{55,49,57,49},{57,49,64,55},{64,55,78,66},
  {78,66,96,79},{96,79,99,81},{99,81,100,81},{100,81,100,80},
  {100,80,99,78},{99,78,89,60},{89,60,78,41},{78,41,73,34},
  {73,34,72,29},{72,29,72,28},{72,28,72,27},{72,27,71,26},
  {71,26,71,25},{71,25,71,24},{71,24,77,16},{77,16,80,15},
  {80,15,87,16},{87,16,91,19},{91,19,95,29},{95,29,103,46},
  {103,46,114,68},{114,68,118,75},{118,75,119,81},{119,81,120,83},
  {120,83,121,83},{121,83,121,82},{121,82,122,69},{122,69,124,54},
  {124,54,126,34},{126,34,126,28},{126,28,129,21},{129,21,135,18},
  {135,18,139,20},{139,20,143,25},{143,25,142,28},{142,28,140,42},
  {140,42,136,64},{136,64,133,78},{133,78,135,78},{135,78,136,76},
  {136,76,144,67},{144,67,156,51},{156,51,162,45},{162,45,168,38},
  {168,38,172,35},{172,35,180,35},{180,35,185,43},{185,43,183,52},
  {183,52,175,62},{175,62,168,71},{168,71,159,83},{159,83,153,94},
  {153,94,154,94},{154,94,155,94},{155,94,176,90},{176,90,188,88},
  {188,88,201,85},{201,85,208,88},{208,88,208,91},{208,91,206,97},
  {206,97,191,101},{191,101,174,104},{174,104,148,110},{148,110,148,111},
  {148,111,148,111},{148,111,160,112},{160,112,165,112},{165,112,177,112},
  {177,112,200,114},{200,114,205,118},{205,118,209,123},{209,123,208,126},
  {208,126,199,131},{199,131,187,128},{187,128,159,121},{159,121,149,119},
  {149,119,147,119},{147,119,147,120},{147,120,156,128},{156,128,170,141},
  {170,141,189,158},{189,158,190,163},{190,163,188,166},{188,166,185,166},
  {185,166,169,153},{169,153,162,148},{162,148,148,136},{148,136,147,136},
  {147,136,147,137},{147,137,150,142},{150,142,168,168},{168,168,169,176},
  {169,176,168,179},{168,179,163,180},{163,180,158,179},{158,179,148,165},
  {148,165,137,149},{137,149,129,134},{129,134,128,135},{128,135,123,189},
  {123,189,120,192},{120,192,115,194},{115,194,110,191},{110,191,108,185},
  {108,185,110,174},{110,174,113,160},{113,160,116,148},{116,148,118,134},
  {118,134,119,129},{119,129,119,129},{119,129,118,129},{118,129,107,144},
  {107,144,91,166},{91,166,78,180},{78,180,75,181},{75,181,70,178},
  {70,178,70,173},{70,173,73,169},{73,169,91,146},{91,146,102,132},
  {102,132,109,124},{109,124,109,123},{109,123,108,123},{108,123,61,153},
  {61,153,52,155},{52,155,49,151},{49,151,49,146},{49,146,51,144},
  {51,144,65,134},{65,134,65,134},
};

// ═════════════════════════════════════════════════════════════
//  HELPERS
// ═════════════════════════════════════════════════════════════

int speedMs(int ms) {
  if (animSpeed == 3) return ms / 2;
  if (animSpeed == 1) return ms * 2;
  return ms;
}

uint16_t hexToRgb565(String hex) {
  hex.replace("#", "");
  if (hex.length() != 6) return C_WHITE;
  long v = strtol(hex.c_str(), nullptr, 16);
  return tft.color565((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

void setBacklight(bool on) {
  backlightOn = on;
  digitalWrite(TFT_BLK, on ? HIGH : LOW);
}

void initColours() {
  // C_ORANGE = tft.color565(170, 72, 28);
  C_ORANGE = tft.color565(218, 17, 0);
  C_DARKBG = tft.color565(10,  12,  16);
  C_MUTED  = tft.color565(90,  88,  86);
  C_GREEN  = tft.color565(80, 220, 130);
  animBgColor = C_ORANGE;
  drawBgColor = C_ORANGE;
}

// ═════════════════════════════════════════════════════════════
//  WIFI + MONITOR
// ═════════════════════════════════════════════════════════════

const char* monitorStateStr() {
  switch (monitorState) {
    case MON_THINKING: return "thinking";
    case MON_WORKING:  return "working";
    case MON_ALERT:    return "alert";
    case MON_OFFLINE:  return "offline";
    default:           return "idle";
  }
}

void loadWifiCredentials() {
  savedSSID = prefs.getString("ssid", "");
  savedPASS = prefs.getString("pass", "");
}

void saveWifiCredentials(const String& ssid, const String& pass) {
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  savedSSID = ssid;
  savedPASS = pass;
}

bool connectSTA() {
  if (savedSSID.length() == 0) {
    staConnected = false;
    staIP = "";
    return false;
  }
  WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < STA_CONNECT_TIMEOUT_MS) {
    delay(200);
  }
  staConnected = (WiFi.status() == WL_CONNECTED);
  staIP = staConnected ? WiFi.localIP().toString() : "";
  return staConnected;
}

void drawWifiScreen() {
  tft.fillScreen(C_DARKBG);
  tft.fillRect(0, 0, DISP_W, 4, C_ORANGE);
  tft.setTextColor(C_WHITE); tft.setTextSize(2);
  tft.setCursor(12, 16);  tft.print("WiFi: ClaWD-Mochi");
  tft.setTextColor(C_MUTED); tft.setTextSize(1);
  tft.setCursor(12, 44);  tft.print("password: clawd1234");
  tft.setTextColor(C_WHITE); tft.setTextSize(1);
  tft.setCursor(12, 62);  tft.print("Controller:");
  tft.setTextColor(C_ORANGE); tft.setTextSize(2);
  tft.setCursor(12, 78);  tft.print("192.168.4.1");
  tft.setTextColor(C_WHITE); tft.setTextSize(1);
  tft.setCursor(12, 108); tft.print("Home WiFi:");
  if (staConnected) {
    tft.setTextColor(C_GREEN); tft.setTextSize(2);
    tft.setCursor(12, 124); tft.print(staIP);
    tft.setTextColor(C_MUTED); tft.setTextSize(1);
    tft.setCursor(12, 152); tft.print("Hook: /status?s=...");
  } else {
    tft.setTextColor(C_MUTED); tft.setTextSize(1);
    tft.setCursor(12, 124); tft.print("not connected");
    tft.setCursor(12, 140); tft.print("configure in web portal");
  }
  tft.setTextColor(C_MUTED); tft.setTextSize(1);
  tft.setCursor(12, 210); tft.print("press any button to start");
}

// ═════════════════════════════════════════════════════════════
//  LOGO
// ═════════════════════════════════════════════════════════════

void drawLogoFilled(uint16_t bg, uint16_t fg) {
  tft.fillScreen(bg);
  for (uint16_t i = 0; i < LOGO_TRI_COUNT; i++) {
    tft.fillTriangle(
      pgm_read_word(&LOGO_TRIS[i][0]), pgm_read_word(&LOGO_TRIS[i][1]),
      pgm_read_word(&LOGO_TRIS[i][2]), pgm_read_word(&LOGO_TRIS[i][3]),
      pgm_read_word(&LOGO_TRIS[i][4]), pgm_read_word(&LOGO_TRIS[i][5]),
      fg);
  }
  tft.setTextColor(fg); tft.setTextSize(2);
  tft.setCursor(LOGO_CX - 54, 210); tft.print("Anthropic");
  tft.setCursor(LOGO_CX - 53, 210); tft.print("Anthropic");
}

// ═════════════════════════════════════════════════════════════
//  VIEWS
// ═════════════════════════════════════════════════════════════

// Eye helpers — shared constants via #define EYE_*
inline int16_t eyeLX(int16_t ox) {
  return (DISP_W - (EYE_W * 2 + EYE_GAP)) / 2 + EYE_OX + ox;
}
inline int16_t eyeRX(int16_t ox) { return eyeLX(ox) + EYE_W + EYE_GAP; }
inline int16_t eyeY()            { return (DISP_H - EYE_H) / 2 - EYE_OY; }
inline int16_t eyeCY()           { return eyeY() + EYE_H / 2; }

uint8_t expressionRenderKey() {
  return (uint8_t)(monitorState | (currentIdleExpr << 3));
}

void invalidateExpressionCanvas() {
  lastRenderKey = 255;
  prevEyeL.valid = false;
  prevEyeR.valid = false;
  lastTickOx = -999;
  lastTickScanOx = -999;
  lastTickSquishState = 255;
  lastTickHeartScale = 255;
  lastSleepyTop = -999;
  lastSleepyZ = 255;
}

void ensureFullExpressionBg() {
  const uint8_t key = expressionRenderKey();
  if (key != lastRenderKey) {
    tft.fillScreen(animBgColor);
    lastRenderKey = key;
    prevEyeL.valid = false;
    prevEyeR.valid = false;
    lastTickOx = -999;
    lastTickScanOx = -999;
    lastTickSquishState = 255;
    lastTickHeartScale = 255;
    lastSleepyTop = -999;
    lastSleepyZ = 255;
  }
}

void clearExpressionZone() {
  tft.fillRect(0, EXPR_ZONE_Y, DISP_W, EXPR_ZONE_H, animBgColor);
  prevEyeL.valid = false;
  prevEyeR.valid = false;
}

void erasePrevEyeRects() {
  if (prevEyeL.valid) {
    tft.fillRect(prevEyeL.x - 3, prevEyeL.y - 3, prevEyeL.w + 6, prevEyeL.h + 6, animBgColor);
    tft.fillRect(prevEyeR.x - 3, prevEyeR.y - 3, prevEyeR.w + 6, prevEyeR.h + 6, animBgColor);
  }
}

void storePrevEyeRects(int16_t lx, int16_t rx, int16_t ey, int16_t w, int16_t h) {
  prevEyeL = {lx, ey, w, h, true};
  prevEyeR = {rx, ey, w, h, true};
}

void drawNormalEyes(int16_t ox = 0, bool blink = false, bool optimized = false) {
  if (optimized) {
    if (ox == lastTickOx && blink == lastTickBlink) return;
    ensureFullExpressionBg();
    erasePrevEyeRects();
  } else {
    tft.fillScreen(animBgColor);
  }
  const int16_t lx = eyeLX(ox), rx = eyeRX(ox), ey = eyeY();
  if (!blink) {
    tft.fillRect(lx, ey, EYE_W, EYE_H, C_BLACK);
    tft.fillRect(rx, ey, EYE_W, EYE_H, C_BLACK);
    if (optimized) storePrevEyeRects(lx, rx, ey, EYE_W, EYE_H);
  } else {
    const int16_t by = ey + EYE_H / 2 - 3;
    tft.fillRect(lx, by, EYE_W, 6, C_BLACK);
    tft.fillRect(rx, by, EYE_W, 6, C_BLACK);
    if (optimized) storePrevEyeRects(lx, rx, by, EYE_W, 6);
  }
  if (optimized) { lastTickOx = ox; lastTickBlink = blink; }
}

void drawChevron(int16_t cx, int16_t cy, int16_t arm, int16_t reach,
                 uint8_t thk, bool rightFacing, uint16_t col) {
  for (int8_t t = -(int8_t)thk; t <= (int8_t)thk; t++) {
    if (rightFacing) {
      tft.drawLine(cx - reach/2, cy - arm + t, cx + reach/2, cy + t,      col);
      tft.drawLine(cx + reach/2, cy + t,       cx - reach/2, cy + arm + t, col);
    } else {
      tft.drawLine(cx + reach/2, cy - arm + t, cx - reach/2, cy + t,      col);
      tft.drawLine(cx - reach/2, cy + t,       cx + reach/2, cy + arm + t, col);
    }
  }
}

void drawSquishEyes(bool closed = false, bool optimized = false) {
  if (optimized) {
    if ((uint8_t)closed == lastTickSquishState) return;
    ensureFullExpressionBg();
    clearExpressionZone();
  } else {
    tft.fillScreen(animBgColor);
  }
  const int16_t lx = eyeLX(0), rx = eyeRX(0), cy = eyeCY();
  const int16_t arm   = EYE_H / 2;
  const int16_t reach = EYE_W / 2;
  const int16_t lcx   = lx + EYE_W / 2;
  const int16_t rcx   = rx + EYE_W / 2;
  if (!closed) {
    drawChevron(lcx, cy, arm, reach, 10, true,  C_BLACK);
    drawChevron(rcx, cy, arm, reach, 10, false, C_BLACK);
  } else {
    tft.fillRect(lx, cy - 5, EYE_W, 10, C_BLACK);
    tft.fillRect(rx, cy - 5, EYE_W, 10, C_BLACK);
  }
  if (optimized) lastTickSquishState = (uint8_t)closed;
}

void drawCodeView() {
  termMode = false;
  tft.fillScreen(C_DARKBG);
  tft.fillRect(0, 0,          DISP_W, 4, C_ORANGE);
  tft.fillRect(0, DISP_H - 4, DISP_W, 4, C_ORANGE);
  tft.setTextColor(C_ORANGE); tft.setTextSize(4);
  tft.setCursor((DISP_W - 144) / 2, DISP_H / 2 - 52); tft.print("Claude");
  tft.setTextColor(C_WHITE);  tft.setTextSize(4);
  tft.setCursor((DISP_W - 96) / 2,  DISP_H / 2 + 8);  tft.print("Code");
  tft.fillRect((DISP_W - 96) / 2, DISP_H / 2 + 52, 96, 3, C_ORANGE);
}

// ── Extended idle / work expressions ───────────────────────────

inline int16_t scanEyeLX(int16_t ox) {
  return (DISP_W - (SCAN_EYE_W * 2 + EYE_GAP)) / 2 + EYE_OX + ox;
}
inline int16_t scanEyeRX(int16_t ox) { return scanEyeLX(ox) + SCAN_EYE_W + EYE_GAP; }

void drawHeartAt(int16_t cx, int16_t cy, uint8_t scale, uint16_t col) {
  static const uint8_t HEART5[5] = { 0b01010, 0b11111, 0b11111, 0b01110, 0b00100 };
  for (int8_t row = 0; row < 5; row++) {
    for (int8_t c = 0; c < 5; c++) {
      if (HEART5[row] & (1 << (4 - c))) {
        tft.fillRect(cx + (c - 2) * scale, cy + (row - 2) * scale, scale, scale, col);
      }
    }
  }
}

void drawHappyArc(int16_t cx, int16_t cy, int16_t w, uint16_t col) {
  const int16_t hw = w / 2;
  for (uint8_t t = 0; t < 4; t++) {
    tft.drawLine(cx - hw, cy + t,     cx,     cy - hw / 2 + t, col);
    tft.drawLine(cx,     cy - hw / 2 + t, cx + hw, cy + t,     col);
  }
}

void drawSleepyEyes(uint8_t zCount = 3, int16_t breathOffset = 0, bool optimized = false) {
  const int16_t sleepyH = 20;
  const int16_t lx = eyeLX(0), rx = eyeRX(0);
  const int16_t top = eyeY() + breathOffset + (EYE_H - sleepyH) / 2;
  const int16_t zBaseY = eyeY() + EYE_H + 10;

  if (optimized) {
    if (top == lastSleepyTop && zCount == lastSleepyZ) return;
    ensureFullExpressionBg();
    erasePrevEyeRects();
    if (zCount != lastSleepyZ) {
      tft.fillRect(DISP_W / 2 - 32, zBaseY - 2, 64, 20, animBgColor);
    }
  } else {
    tft.fillScreen(animBgColor);
  }

  tft.fillRect(lx, top, EYE_W, sleepyH, C_BLACK);
  tft.fillRect(rx, top, EYE_W, sleepyH, C_BLACK);

  if (optimized) {
    storePrevEyeRects(lx, rx, top, EYE_W, sleepyH);
    lastSleepyTop = top;
    lastSleepyZ = zCount;
  }

  if (zCount > 0) {
    if (optimized) {
      tft.fillRect(DISP_W / 2 - 32, zBaseY - 2, 64, 20, animBgColor);
    }
    tft.setTextColor(C_WHITE);
    tft.setTextSize(2);
    tft.setCursor(DISP_W / 2 - 24, zBaseY);
    tft.print("Z");
    if (zCount > 1) tft.print(" z");
    if (zCount > 2) tft.print(" z");
  }
}

void drawHeartEyes(uint8_t scale = 6, bool optimized = false) {
  if (optimized) {
    if (scale == lastTickHeartScale) return;
    ensureFullExpressionBg();
    clearExpressionZone();
  } else {
    tft.fillScreen(animBgColor);
  }
  const int16_t lcx = eyeLX(0) + EYE_W / 2;
  const int16_t rcx = eyeRX(0) + EYE_W / 2;
  const int16_t cy  = eyeCY();
  drawHeartAt(lcx, cy, scale, C_BLACK);
  drawHeartAt(rcx, cy, scale, C_BLACK);
  if (optimized) lastTickHeartScale = scale;
}

void drawHappyEyes(int16_t ox = 0, bool showStars = false, bool optimized = false) {
  if (optimized) {
    ensureFullExpressionBg();
    clearExpressionZone();
  } else {
    tft.fillScreen(animBgColor);
  }
  const int16_t lx = eyeLX(ox) + EYE_W / 2;
  const int16_t rx = eyeRX(ox) + EYE_W / 2;
  const int16_t cy = eyeCY() + 8;
  drawHappyArc(lx, cy, 30, C_BLACK);
  drawHappyArc(rx, cy, 30, C_BLACK);
  if (showStars) {
    tft.fillRect(lx - 18, cy - 28, 4, 4, C_WHITE);
    tft.fillRect(rx + 14, cy - 26, 3, 3, C_WHITE);
    tft.fillRect(DISP_W / 2 - 2, eyeY() - 8, 4, 4, C_WHITE);
  }
}

void drawScanEyes(int16_t ox = 0, bool optimized = false) {
  if (optimized) {
    if (ox == lastTickScanOx) return;
    ensureFullExpressionBg();
    erasePrevEyeRects();
  } else {
    tft.fillScreen(animBgColor);
  }
  const int16_t lx = scanEyeLX(ox), rx = scanEyeRX(ox), ey = eyeY();
  tft.fillRect(lx, ey, SCAN_EYE_W, EYE_H, C_BLACK);
  tft.fillRect(rx, ey, SCAN_EYE_W, EYE_H, C_BLACK);
  if (optimized) {
    storePrevEyeRects(lx, rx, ey, SCAN_EYE_W, EYE_H);
    lastTickScanOx = ox;
  }
}

void drawSparkles(uint8_t count = 10) {
  for (uint8_t i = 0; i < count; i++) {
    const int16_t x = random(4, DISP_W - 4);
    const int16_t y = random(4, DISP_H - 4);
    const uint8_t sz = random(2, 5);
    const uint16_t col = (random(2) == 0) ? C_WHITE : C_ORANGE;
    tft.fillRect(x, y, sz, sz, col);
    if (sz > 2) {
      tft.drawPixel(x + sz, y + sz / 2, col);
      tft.drawPixel(x + sz / 2, y + sz, col);
    }
  }
}

// ═════════════════════════════════════════════════════════════
//  EYE RIG ENGINE
// ═════════════════════════════════════════════════════════════

int16_t rigK() {               // 弹簧刚度(/256),随 animSpeed
  if (animSpeed == 3) return 72;
  if (animSpeed == 1) return 30;
  return 48;
}

void springTo(Spring &s, int32_t target) {   // target 为 8.8
  s.vel += ((target - s.cur) * rigK()) >> 8;
  s.vel  = (s.vel * RIG_DAMP) >> 8;
  s.cur += s.vel;
}

void springSnap(Spring &s, int32_t target) { s.cur = target; s.vel = 0; }

inline int16_t rigLCX(int16_t ox) { return eyeLX(ox) + EYE_W / 2; }
inline int16_t rigRCX(int16_t ox) { return eyeRX(ox) + EYE_W / 2; }

void rigInvalidate() {
  rig.prevValid = false;
  rig.zoneDirty = true;
}

void rigSnapPose(const EyePose &p, uint8_t flags) {
  rig.pose = p; rig.flags = flags; rig.trans = 0;
  springSnap(rig.ox,  (int32_t)p.ox  << 8);
  springSnap(rig.oy,  (int32_t)p.oy  << 8);
  springSnap(rig.w,   (int32_t)p.w   << 8);
  springSnap(rig.h,   (int32_t)p.h   << 8);
  springSnap(rig.lid, (int32_t)p.lid << 8);
  rig.drawnStyle = p.style;
  rig.blinkFrame = 0;
  rigInvalidate();
  rigBehNextMs = 0;
  rigBehStep = 0;
}

void rigSetPose(const EyePose &p, uint8_t flags) {
  if (p.style != rig.drawnStyle) {
    rig.trans = 1;                 // 闭眼→换样式→睁眼(过渡中再调用则更新目标)
    rig.transNext = p;
    rig.transFlagsNext = flags;
    rigBehNextMs = 0;
    rigBehStep = 0;
    return;
  }
  if (rig.trans == 1) rig.trans = 0;   // 过渡中改回同样式:取消闭眼
  rig.pose = p;
  rig.flags = flags;
  rigBehNextMs = 0;
  rigBehStep = 0;
}

int16_t rigBreathOffset() {
  if (!(rig.flags & (RIG_BREATH | RIG_BREATH2))) return 0;
  int16_t v = BREATH_TAB[(rig.breathPhase >> 8) & 15];
  if (rig.flags & RIG_BREATH2) v *= 2;
  return v;
}

void rigTick(unsigned long now) {
  // 样式过渡
  if (rig.trans == 1) {
    springTo(rig.lid, (int32_t)240 << 8);
    if ((rig.lid.cur >> 8) >= 225) {
      rig.pose  = rig.transNext;
      rig.flags = rig.transFlagsNext;
      springSnap(rig.ox, (int32_t)rig.pose.ox << 8);
      springSnap(rig.oy, (int32_t)rig.pose.oy << 8);
      springSnap(rig.w,  (int32_t)rig.pose.w  << 8);
      springSnap(rig.h,  (int32_t)rig.pose.h  << 8);
      rig.drawnStyle = rig.pose.style;
      rig.zoneDirty = true;
      rig.trans = 2;
    }
  } else {
    if (rig.trans == 2 && (rig.lid.cur >> 8) <= rig.pose.lid + 12) rig.trans = 0;
    springTo(rig.lid, (int32_t)rig.pose.lid << 8);
  }

  // 微扫视
  if ((rig.flags & RIG_SACCADE) && rig.trans == 0) {
    if (now >= rig.nextSaccadeMs) {
      rig.sacX = (int16_t)random(-3, 4);
      rig.nextSaccadeMs = now + 700 + random(1300);
    }
  } else {
    rig.sacX = 0;
  }

  springTo(rig.ox, ((int32_t)(rig.pose.ox + rig.sacX)) << 8);
  springTo(rig.oy, (int32_t)rig.pose.oy << 8);
  springTo(rig.w,  (int32_t)rig.pose.w  << 8);
  springTo(rig.h,  (int32_t)rig.pose.h  << 8);

  // 眨眼
  if (rig.blinkFrame > 0) {
    rig.blinkFrame++;
    if (rig.blinkFrame > BLINK_FRAMES) {
      if (rig.blinkAgain) { rig.blinkFrame = 1; rig.blinkAgain = false; }
      else rig.blinkFrame = 0;
    }
  } else if ((rig.flags & RIG_BLINK) && rig.trans == 0 && now >= rig.nextBlinkMs) {
    rig.blinkFrame = 1;
    rig.blinkAgain = (random(100) < 12);
    rig.nextBlinkMs = now + 2500 + random(3500);
  }

  // 呼吸
  if (rig.flags & (RIG_BREATH | RIG_BREATH2)) {
    rig.breathPhase += (animSpeed == 1) ? 26 : ((animSpeed == 3) ? 60 : 40);
  }
}

void drawRigEye(int16_t cx, int16_t cy, int16_t w, int16_t h, int16_t lid,
                bool rightFacing, EyeRect &out) {
  switch (rig.drawnStyle) {
    case STYLE_CHEVRON: {
      const int16_t arm = ((h / 2) * (240 - lid)) / 240;
      if (arm <= 3) {
        tft.fillRect(cx - w / 2, cy - 4, w, 8, C_BLACK);
        out = {(int16_t)(cx - w / 2), (int16_t)(cy - 4), w, 8, true};
      } else {
        drawChevron(cx, cy, arm, w, 10, rightFacing, C_BLACK);
        out = {(int16_t)(cx - w / 2 - 2), (int16_t)(cy - arm - 12),
               (int16_t)(w + 4), (int16_t)(arm * 2 + 24), true};
      }
      break;
    }
    case STYLE_ARC:
      drawHappyArc(cx, cy, w, C_BLACK);
      out = {(int16_t)(cx - w / 2), (int16_t)(cy - w / 4 - 2),
             (int16_t)(w + 2), (int16_t)(w / 4 + 8), true};
      break;
    case STYLE_HEART: {
      uint8_t scale = (uint8_t)((w < 3) ? 3 : ((w > 9) ? 9 : w));
      drawHeartAt(cx, cy, scale, C_BLACK);
      out = {(int16_t)(cx - scale * 3), (int16_t)(cy - scale * 3),
             (int16_t)(scale * 6), (int16_t)(scale * 6), true};
      break;
    }
    default: {  // STYLE_RECT — 眼睑自上而下
      int16_t vis = (int16_t)((int32_t)h * (240 - lid) / 240);
      if (vis < 5) vis = 5;
      const int16_t top = cy - h / 2 + (h - vis);
      tft.fillRect(cx - w / 2, top, w, vis, C_BLACK);
      out = {(int16_t)(cx - w / 2), top, w, vis, true};
      break;
    }
  }
}

void drawRig() {
  int16_t ox  = rig.ox.cur >> 8;
  int16_t oy  = (rig.oy.cur >> 8) + rigBreathOffset();
  int16_t w   = rig.w.cur >> 8;   if (w < 4) w = 4;
  int16_t h   = rig.h.cur >> 8;   if (h < 4) h = 4;
  int16_t lid = rig.lid.cur >> 8;
  if (lid < 0) lid = 0;
  if (lid > 240) lid = 240;

  if (rig.blinkFrame > 0) {       // 挤压拉伸眨眼,叠加在眼睑之上
    h = (int16_t)((int32_t)h * BLINK_H_PCT[rig.blinkFrame - 1] / 100);
    w = (int16_t)((int32_t)w * BLINK_W_PCT[rig.blinkFrame - 1] / 100);
    if (h < 4) h = 4;
  }

  rigZoneCleared = false;
  if (rig.zoneDirty) {
    tft.fillRect(0, EXPR_ZONE_Y, DISP_W, EXPR_ZONE_H, animBgColor);
    rig.prevValid = false;
    rig.zoneDirty = false;
    rigZoneCleared = true;
  }
  if (rig.prevValid) {
    tft.fillRect(rig.prevL.x - 2, rig.prevL.y - 2, rig.prevL.w + 4, rig.prevL.h + 4, animBgColor);
    tft.fillRect(rig.prevR.x - 2, rig.prevR.y - 2, rig.prevR.w + 4, rig.prevR.h + 4, animBgColor);
  }

  const int16_t cy = eyeCY() + oy;
  drawRigEye(rigLCX(ox), cy, w, h, lid, true,  rig.prevL);
  drawRigEye(rigRCX(ox), cy, w, h, lid, false, rig.prevR);
  rig.prevValid = true;
}

// ── 表情选择:状态 → 姿态 + 行为标志 ──────────────────────────
void rigApplyExpression(bool snap) {
  EyePose p = POSE_NORMAL;
  uint8_t f = RIG_BLINK | RIG_SACCADE | RIG_BREATH;

  if (monitorState == MON_IDLE) {
    switch (currentIdleExpr) {
      case IDLE_SLEEPY: p = POSE_SLEEPY; f = RIG_BREATH2;  break;
      case IDLE_HEART:  p = POSE_HEART;  f = 0;            break;
      case IDLE_HAPPY:  p = POSE_HAPPY;  f = RIG_BREATH;   break;
      default: break;
    }
  } else if (monitorState == MON_THINKING) {
    p = POSE_THINK; f = RIG_BREATH;
  } else if (monitorState == MON_WORKING) {
    switch (workAct) {
      case ACT_READ: p = POSE_READ; f = RIG_BLINK; break;
      case ACT_EDIT: p = POSE_EDIT; f = RIG_BLINK; break;
      case ACT_RUN:  p = POSE_RUN;  f = 0;         break;
      case ACT_NET:  p = POSE_NET;  f = RIG_BLINK; break;
      default:       p = POSE_SCAN; f = 0;         break;
    }
  }

  if (snap) rigSnapPose(p, f);
  else      rigSetPose(p, f);
}

// ── 脚本化行为:周期性挪动目标姿态,弹簧负责平滑 ─────────────
void rigBehaviorTick(unsigned long now) {
  if (rig.trans != 0) return;

  if (monitorState == MON_IDLE) {
    switch (currentIdleExpr) {
      case IDLE_HEART:                    // 心跳脉冲
        if (now >= rigBehNextMs) {
          rig.pose.w = (rig.pose.w == 6) ? 7 : 6;
          rig.pose.h = rig.pose.w;
          rigBehNextMs = now + 600;
        }
        break;
      case IDLE_HAPPY: {                  // 缓动摇摆
        static const int8_t sway[8] = {-6, -3, 0, 3, 6, 3, 0, -3};
        if (now >= rigBehNextMs) { rig.pose.ox = sway[rigBehStep & 7]; rigBehStep++; rigBehNextMs = now + 260; }
        break;
      }
      default: break;                     // 普通/困倦:噪声层足够
    }
    return;
  }

  if (monitorState == MON_THINKING) {     // chevron 开合
    if (now >= rigBehNextMs) {
      rig.pose.lid = (rig.pose.lid == 0) ? 240 : 0;
      rigBehNextMs = now + 900;
    }
    return;
  }

  if (monitorState == MON_WORKING) {
    switch (workAct) {
      case ACT_READ: {                    // 低头扫读
        static const int8_t readoy[6] = {22, 26, 30, 34, 30, 26};
        if (now >= rigBehNextMs) { rig.pose.oy = readoy[rigBehStep % 6]; rigBehStep++; rigBehNextMs = now + 420; }
        break;
      }
      case ACT_RUN:                       // 紧绷快速小扫视
        if (now >= rigBehNextMs) { rig.pose.ox = (int16_t)random(-8, 9); rigBehNextMs = now + 260 + random(160); }
        break;
      case ACT_NET:                       // 大幅东张西望
        if (now >= rigBehNextMs) { rig.pose.ox = random(2) ? 24 : -24; rigBehNextMs = now + 500 + random(400); }
        break;
      case ACT_EDIT:                      // 眼睛稳住,光标覆盖层闪烁
        break;
      default: {                          // ACT_WORK / ACT_AGENT:经典扫视
        static const int8_t scan[10] = {-28, -18, -8, 2, 12, 22, 28, 16, 2, -14};
        if (now >= rigBehNextMs) { rig.pose.ox = scan[rigBehStep % 10]; rigBehStep++; rigBehNextMs = now + 180; }
        break;
      }
    }
  }
}

// ── 覆盖层:困倦 Z 字、edit 光标、开心眼星星 ─────────────────
void rigOverlayTick() {
  static uint8_t lastZ = 255;
  static bool caretOn = false;
  static unsigned long caretMs = 0;

  if (monitorState == MON_IDLE && currentIdleExpr == IDLE_SLEEPY && rig.trans == 0) {
    const uint8_t z = 1 + (uint8_t)((millis() / 1400) % 3);
    if (z != lastZ || rigZoneCleared) {
      const int16_t zy = eyeY() + EYE_H + 14;
      tft.fillRect(DISP_W / 2 - 36, zy - 2, 72, 20, animBgColor);
      tft.setTextColor(C_WHITE);
      tft.setTextSize(2);
      tft.setCursor(DISP_W / 2 - 24, zy);
      tft.print("Z");
      if (z > 1) tft.print(" z");
      if (z > 2) tft.print(" z");
      lastZ = z;
    }
  } else if (lastZ != 255) {
    const int16_t zy = eyeY() + EYE_H + 14;
    tft.fillRect(DISP_W / 2 - 36, zy - 2, 72, 20, animBgColor);
    lastZ = 255;
  }

  static bool caretDrawn = false;
  if (monitorState == MON_WORKING && workAct == ACT_EDIT) {
    const unsigned long now = millis();
    if (now - caretMs >= 530 || rigZoneCleared) {
      if (now - caretMs >= 530) { caretOn = !caretOn; caretMs = now; }
      tft.fillRect(206, 196, 10, 16, caretOn ? C_GREEN : animBgColor);
      caretDrawn = true;
    }
  } else if (caretDrawn) {
    tft.fillRect(206, 196, 10, 16, animBgColor);
    caretDrawn = false;
  }

  // 开心眼:偶发星星(位置避开弧线眼的脏区包围盒)
  static unsigned long starMs = 0;
  static bool starsOn = false;
  if (monitorState == MON_IDLE && currentIdleExpr == IDLE_HAPPY && rig.trans == 0) {
    const unsigned long snow = millis();
    if (!starsOn && snow - starMs > 3600) {
      starsOn = true; starMs = snow;
      tft.fillRect(rigLCX(0) - 18, eyeCY() - 24, 4, 4, C_WHITE);
      tft.fillRect(rigRCX(0) + 14, eyeCY() - 22, 3, 3, C_WHITE);
    } else if (starsOn && snow - starMs > 450) {
      starsOn = false; starMs = snow;
      tft.fillRect(rigLCX(0) - 18, eyeCY() - 24, 4, 4, animBgColor);
      tft.fillRect(rigRCX(0) + 14, eyeCY() - 22, 3, 3, animBgColor);
    }
  } else {
    starsOn = false;   // 离开 happy 后区域由 zone 清理负责
  }
}

// ═════════════════════════════════════════════════════════════
//  TERMINAL
// ═════════════════════════════════════════════════════════════

void termClear() {
  for (uint8_t i = 0; i < TERM_ROWS; i++) termLines[i] = "";
  termRow = 0; termCol = 0;
}

void termDrawHeader() {
  tft.fillRect(0, 0, DISP_W, TERM_PAD_Y + 1, C_DARKBG);
  tft.setTextColor(C_ORANGE); tft.setTextSize(1);
  tft.setCursor(TERM_PAD_X, 4); tft.print("clawd@mochi terminal");
  tft.drawFastHLine(0, TERM_PAD_Y, DISP_W, C_ORANGE);
}

// Prefix "clawd:~$ " in green, drawn only when the row has content
void termDrawPrefix(int16_t yy) {
  tft.setTextColor(C_GREEN); tft.setTextSize(1);
  tft.setCursor(TERM_PAD_X, yy + 6);
  tft.print("clawd:~$ ");
}

#define PREFIX_PX 54   // 9 chars × 6px = 54px at textSize 1

void termDrawLine(uint8_t r) {
  const int16_t yy = TERM_PAD_Y + 4 + r * TERM_CHAR_H;
  tft.fillRect(0, yy, DISP_W, TERM_CHAR_H, C_DARKBG);
  // show prefix only on the currently active (cursor) line
  if (r == termRow) termDrawPrefix(yy);
  tft.setTextColor(C_WHITE); tft.setTextSize(2);
  tft.setCursor(TERM_PAD_X + PREFIX_PX, yy + 1);
  tft.print(termLines[r]);
  if (r == termRow) {
    const int16_t cx = TERM_PAD_X + PREFIX_PX + termCol * TERM_CHAR_W;
    tft.fillRect(cx, yy + 1, TERM_CHAR_W - 2, TERM_CHAR_H - 2, C_GREEN);
  }
}

void termDrawLastChar() {
  if (termCol == 0) return;
  const int16_t yy    = TERM_PAD_Y + 4 + termRow * TERM_CHAR_H;
  const int16_t baseX = TERM_PAD_X + PREFIX_PX;
  const uint8_t prev  = termCol - 1;
  // erase prev cell (had cursor block)
  tft.fillRect(baseX + prev * TERM_CHAR_W, yy + 1, TERM_CHAR_W, TERM_CHAR_H - 1, C_DARKBG);
  tft.setTextColor(C_WHITE); tft.setTextSize(2);
  tft.setCursor(baseX + prev * TERM_CHAR_W, yy + 1);
  tft.print(termLines[termRow][prev]);
  // new cursor
  tft.fillRect(baseX + termCol * TERM_CHAR_W, yy + 1, TERM_CHAR_W - 2, TERM_CHAR_H - 2, C_GREEN);
}

void termDrawBackspace() {
  const int16_t yy    = TERM_PAD_Y + 4 + termRow * TERM_CHAR_H;
  const int16_t baseX = TERM_PAD_X + PREFIX_PX;
  // erase deleted char + old cursor
  tft.fillRect(baseX + termCol * TERM_CHAR_W, yy + 1, TERM_CHAR_W * 2, TERM_CHAR_H - 1, C_DARKBG);
  // new cursor
  tft.fillRect(baseX + termCol * TERM_CHAR_W, yy + 1, TERM_CHAR_W - 2, TERM_CHAR_H - 2, C_GREEN);
  // if line now empty, erase the prefix too
  if (termLines[termRow].length() == 0) {
    tft.fillRect(0, yy, TERM_PAD_X + PREFIX_PX, TERM_CHAR_H, C_DARKBG);
  }
}

void termFullRedraw() {
  tft.fillScreen(C_DARKBG);
  termDrawHeader();
  for (uint8_t r = 0; r < TERM_ROWS; r++) termDrawLine(r);
}

void termScroll() {
  for (uint8_t i = 0; i < TERM_ROWS - 1; i++) termLines[i] = termLines[i + 1];
  termLines[TERM_ROWS - 1] = "";
  termRow = TERM_ROWS - 1;
  termFullRedraw();
}

void termAddChar(char c) {
  if (c == '\n' || c == '\r') {
    const int16_t yy = TERM_PAD_Y + 4 + termRow * TERM_CHAR_H;
    // erase cursor on current row
    tft.fillRect(TERM_PAD_X + PREFIX_PX + termCol * TERM_CHAR_W,
                 yy + 1, TERM_CHAR_W, TERM_CHAR_H - 1, C_DARKBG);
    termRow++; termCol = 0;
    if (termRow >= TERM_ROWS) { termScroll(); return; }
    termDrawLine(termRow);  // draws prefix on new line
  } else if (c == '\b' || c == 127) {
    if (termCol > 0) {
      termCol--;
      termLines[termRow].remove(termLines[termRow].length() - 1);
      termDrawBackspace();
    }
  } else if (c >= 32 && c < 127) {
    if (termCol >= TERM_COLS) {
      termRow++; termCol = 0;
      if (termRow >= TERM_ROWS) { termScroll(); return; }
    }
    // draw prefix on first char of this line
    if (termCol == 0) termDrawPrefix(TERM_PAD_Y + 4 + termRow * TERM_CHAR_H);
    termLines[termRow] += c;
    termCol++;
    termDrawLastChar();
  }
}

// ═════════════════════════════════════════════════════════════
//  ANIMATIONS
// ═════════════════════════════════════════════════════════════

void animNormalEyes() {
  busy = true;
  const int16_t offs[] = {-16, 16, -16, 16, 0};
  for (uint8_t i = 0; i < 5; i++) { drawNormalEyes(offs[i]); delay(speedMs(80)); }
  drawNormalEyes(0, true);  delay(speedMs(100));
  drawNormalEyes(0, false); delay(speedMs(70));
  drawNormalEyes(0, true);  delay(speedMs(70));
  drawNormalEyes(0, false);
  busy = false;
}

void animSquishEyes() {
  busy = true;
  for (uint8_t i = 0; i < 3; i++) {
    drawSquishEyes(false); delay(speedMs(160));
    drawSquishEyes(true);  delay(speedMs(100));
  }
  drawSquishEyes(false);
  busy = false;
}

void animLogoReveal() {
  busy = true;
  tft.fillScreen(animBgColor);
  for (uint16_t i = 0; i < LOGO_SEG_COUNT; i++) {
    int16_t x1 = pgm_read_word(&LOGO_SEGS[i][0]);
    int16_t y1 = pgm_read_word(&LOGO_SEGS[i][1]);
    int16_t x2 = pgm_read_word(&LOGO_SEGS[i][2]);
    int16_t y2 = pgm_read_word(&LOGO_SEGS[i][3]);
    tft.drawLine(x1, y1, x2, y2, C_WHITE);
    tft.drawLine(x1 + 1, y1, x2 + 1, y2, C_WHITE);
    if (i % 4 == 0) { server.handleClient(); delay(speedMs(8)); }
  }
  drawLogoFilled(animBgColor, C_WHITE);
  delay(1500);
  busy = false;
}

void resetIdleRotation() {
  lastIdleSwitch = millis();
  nextIdleSwitchMs = IDLE_SWITCH_MIN_MS +
    random(IDLE_SWITCH_MAX_MS - IDLE_SWITCH_MIN_MS + 1);
}

int animTickMs() {
  if (animSpeed == 3) return 45;
  if (animSpeed == 1) return 110;
  return 70;
}

void tickMonitorAnimation() {
  if (currentView != VIEW_MONITOR) return;
  if (busy) return;
  if (monitorState == MON_OFFLINE || monitorState == MON_ALERT) return;

  const unsigned long now = millis();
  if (lastAnimTick != 0 && now - lastAnimTick < RIG_TICK_MS) return;
  lastAnimTick = now;

  rigBehaviorTick(now);
  rigTick(now);
  drawRig();
  rigOverlayTick();
}

void animSleepyEyes() {
  busy = true;
  for (uint8_t r = 0; r < 3; r++) {
    for (int8_t b = 0; b >= -4; b--) {
      drawSleepyEyes(0, b);
      delay(speedMs(120));
      server.handleClient();
    }
    for (int8_t b = -4; b <= 0; b++) {
      drawSleepyEyes(0, b);
      delay(speedMs(120));
      server.handleClient();
    }
  }
  for (uint8_t z = 1; z <= 3; z++) {
    drawSleepyEyes(z, 0);
    delay(speedMs(200));
    server.handleClient();
  }
  busy = false;
}

void animHeartEyes() {
  busy = true;
  const uint8_t scales[] = {6, 7, 6, 7, 6};
  for (uint8_t i = 0; i < 5; i++) {
    drawHeartEyes(scales[i]);
    delay(speedMs(180));
    server.handleClient();
  }
  drawHeartEyes(6);
  busy = false;
}

void animHappyEyes() {
  busy = true;
  const int16_t offs[] = {-6, 6, -4, 4, 0};
  for (uint8_t i = 0; i < 5; i++) {
    drawHappyEyes(offs[i], i == 2);
    delay(speedMs(140));
    server.handleClient();
  }
  drawHappyEyes(0, false);
  busy = false;
}

void animScanEyes() {
  busy = true;
  const int16_t scan[] = {-28, 28, -28, 28, -20, 20, -12, 12, 0};
  for (uint8_t i = 0; i < 9; i++) {
    drawScanEyes(scan[i]);
    delay(speedMs(80));
    server.handleClient();
  }
  drawScanEyes(0);
  busy = false;
}

void animDoneSparkle() {
  busy = true;
  static const int16_t grow[5] = {8, 20, 36, 26, 30};
  for (uint8_t i = 0; i < 5; i++) {
    tft.fillScreen(animBgColor);
    drawHappyArc(eyeLX(0) + EYE_W / 2, eyeCY() + 8, grow[i], C_BLACK);
    drawHappyArc(eyeRX(0) + EYE_W / 2, eyeCY() + 8, grow[i], C_BLACK);
    delay(speedMs(60));
    server.handleClient();
  }
  drawHappyEyes(0, false);
  delay(speedMs(200));
  for (uint8_t f = 0; f < 4; f++) {
    drawHappyEyes(0, f % 2 == 1);
    drawSparkles(8 + random(5));
    delay(speedMs(250));
    server.handleClient();
  }
  drawHappyEyes(0, true);
  delay(speedMs(2000));
  busy = false;
  monitorState = MON_IDLE;
  currentIdleExpr = IDLE_NORMAL;
  currentIdleIndex = 0;
  resetIdleRotation();
  rigApplyExpression(true);
}

void playIdleExpression(uint8_t expr) {
  currentIdleExpr = expr;
  switch (expr) {
    case IDLE_SLEEPY: animSleepyEyes(); break;
    case IDLE_HEART:  animHeartEyes();  break;
    case IDLE_HAPPY:  animHappyEyes();  break;
    default:          animNormalEyes(); break;
  }
}

void checkIdleRotation() {
  if (currentView != VIEW_MONITOR) return;
  if (monitorState != MON_IDLE) return;
  if (busy) return;
  if (lastIdleSwitch == 0) {
    resetIdleRotation();
    return;
  }
  if (millis() - lastIdleSwitch < nextIdleSwitchMs) return;

  currentIdleIndex = (currentIdleIndex + 1) % IDLE_POOL_SIZE;
  currentIdleExpr = IDLE_POOL[currentIdleIndex];
  rigApplyExpression(false);   // 眼睑过渡切表情
  lastIdleSwitch = millis();
  nextIdleSwitchMs = IDLE_SWITCH_MIN_MS +
    random(IDLE_SWITCH_MAX_MS - IDLE_SWITCH_MIN_MS + 1);
}

void applyMonitorState(const String& s) {
  if (busy) return;   // 阻塞动画期间丢弃状态推送,防 done 递归与状态撕裂
  if (s == "done") {
    lastStatusMs = millis();
    statusTimedOut = false;
    if (currentView == VIEW_DRAW) return;
    if (currentView == VIEW_CODE && termMode) return;
    currentView = VIEW_MONITOR;
    termMode = false;
    if (!backlightOn) setBacklight(true);
    animDoneSparkle();
    return;
  }

  if (s == "idle")           monitorState = MON_IDLE;
  else if (s == "thinking")  monitorState = MON_THINKING;
  else if (s == "working")   monitorState = MON_WORKING;
  else if (s == "alert")     monitorState = MON_ALERT;
  else if (s == "offline")   monitorState = MON_OFFLINE;
  else return;

  lastStatusMs = millis();
  statusTimedOut = false;

  if (currentView == VIEW_DRAW) return;
  if (currentView == VIEW_CODE && termMode) return;

  currentView = VIEW_MONITOR;
  termMode = false;

  switch (monitorState) {
    case MON_IDLE:
      if (!backlightOn) setBacklight(true);
      resetIdleRotation();
      rigApplyExpression(false);
      break;
    case MON_THINKING:
    case MON_WORKING:
      if (!backlightOn) setBacklight(true);
      rigApplyExpression(false);
      break;
    case MON_ALERT:
      if (!backlightOn) setBacklight(true);
      animLogoReveal();
      rigInvalidate();
      break;
    case MON_OFFLINE:
      drawNormalEyes();
      setBacklight(false);
      rigInvalidate();
      break;
  }
}

void enterMonitorView() {
  currentView = VIEW_MONITOR;
  termMode = false;
  statusTimedOut = false;
  lastAnimTick = 0;
  if (monitorState == MON_IDLE) resetIdleRotation();
  rigApplyExpression(true);   // snap:进视图立即就位
}

void checkStatusTimeout() {
  if (currentView != VIEW_MONITOR) return;
  if (lastStatusMs == 0) return;
  if (monitorState == MON_IDLE || monitorState == MON_OFFLINE) return;
  if (millis() - lastStatusMs < STATUS_TIMEOUT_MS) return;
  if (statusTimedOut) return;

  statusTimedOut = true;
  monitorState = MON_IDLE;
  if (!backlightOn) setBacklight(true);
  resetIdleRotation();
  rigApplyExpression(false);
}

// ═════════════════════════════════════════════════════════════
//  WEB PAGE
// ═════════════════════════════════════════════════════════════
const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Clawd Mochi</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
body{background:#1c1c20;font-family:'Courier New',monospace;color:#e8e4dc;
  display:flex;flex-direction:column;align-items:center;
  padding:20px 14px 52px;gap:14px;min-height:100vh}

.hdr{text-align:center;padding:2px 0 4px}
.mascot{font-size:15px;color:#c96a3e;line-height:1.3;font-weight:bold;
  font-family:'Courier New',monospace;display:block;letter-spacing:1px}
.sitename{font-size:10px;color:#5a5048;margin-top:8px;letter-spacing:3px}

.sec{width:100%;max-width:390px;font-size:10px;color:#8a8278;
  letter-spacing:2px;font-weight:bold;padding:0 2px}

/* Busy bar */
.busy{width:100%;max-width:390px;height:2px;background:#2e2a28;
  border-radius:1px;overflow:hidden;opacity:0;transition:opacity .2s}
.busy.show{opacity:1}
.busy-i{height:100%;width:30%;background:#c96a3e;border-radius:1px;
  animation:sl 1s linear infinite}
@keyframes sl{0%{margin-left:-30%}100%{margin-left:100%}}

/* Controls */
.ctrl{display:flex;gap:8px;width:100%;max-width:390px}
.cbtn{flex:1;background:#252428;border:1.5px solid #38343a;border-radius:10px;
  color:#b8b4ac;font-family:'Courier New',monospace;font-size:11px;font-weight:bold;
  padding:12px 4px;cursor:pointer;text-align:center;transition:all .12s}
.cbtn:active:not(:disabled){transform:scale(.94)}
.cbtn:disabled{opacity:.3;cursor:default}
.cbtn.on{border-color:#c96a3e;color:#c96a3e;background:#201408}
.cbtn.dim{border-color:#2e2a28;color:#4a4540}

/* View grid */
.vgrid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;width:100%;max-width:390px}
.vbtn{background:#252428;border:1.5px solid #38343a;border-radius:12px;
  color:#d8d4cc;font-family:'Courier New',monospace;
  padding:12px 4px 8px;cursor:pointer;text-align:center;
  transition:all .12s;user-select:none}
.vbtn:active:not(:disabled){transform:scale(.94)}
.vbtn:disabled{opacity:.3;cursor:default}
.vbtn .ic{font-size:18px;display:block;margin-bottom:3px;line-height:1;color:#c96a3e}
.vbtn .nm{font-size:11px;font-weight:bold;color:#e8e4dc}
.vbtn .ht{font-size:8px;color:#8a8278;margin-top:2px}
.vbtn.active{border-color:#c96a3e;background:#201408}
.vbtn[data-v="1"].active{border-color:#c96a3e;background:#201408}
.vbtn[data-v="2"].active{border-color:#4a8acd;background:#0c1628}
.vbtn[data-v="3"].active{border-color:#38343a;background:#201c18}
.vbtn[data-v="4"].active{border-color:#28b878;background:#0c1e12}

/* Speed slider */
.speed-row{width:100%;max-width:390px;display:flex;align-items:center;gap:10px}
.sl{font-size:10px;color:#6a6058;white-space:nowrap;min-width:36px}
input[type=range]{flex:1;accent-color:#c96a3e;cursor:pointer;height:20px}
.sv{font-size:11px;color:#c96a3e;min-width:44px;text-align:right;font-weight:bold}

/* Terminal */
.twrap{width:100%;max-width:390px;display:none;flex-direction:column;gap:8px}
.twrap.open{display:flex}
.thdr{display:flex;justify-content:space-between;align-items:center}
.tttl{font-size:11px;color:#28b878;letter-spacing:1px;font-weight:bold}
.tx{background:#0c1e12;border:2px solid #1a4828;border-radius:9px;
  color:#28b878;font-family:'Courier New',monospace;font-size:13px;
  font-weight:bold;padding:10px 18px;cursor:pointer}
.tx:active{background:#081410}
.trow{display:flex;gap:6px}
.tin{flex:1;background:#0c1018;border:1.5px solid #1a2820;border-radius:9px;
  color:#40d880;font-family:'Courier New',monospace;font-size:15px;
  padding:11px;outline:none}
.tin::placeholder{color:#2a3828}
.tgo{background:#1a9060;border:none;border-radius:9px;color:#fff;
  font-family:'Courier New',monospace;font-size:22px;font-weight:bold;
  padding:11px 16px;cursor:pointer;min-width:52px}
.tgo:active{background:#0f6040}

/* Canvas */
.cwrap{width:100%;max-width:390px;background:#222028;border:1.5px solid #38343a;
  border-radius:12px;padding:12px;flex-direction:column;gap:10px;display:none}
.cwrap.open{display:flex}
.crow{display:flex;gap:8px}
.ci{display:flex;flex-direction:column;align-items:center;gap:4px;flex:1}
.cl{font-size:10px;color:#7a7068;letter-spacing:1px;font-weight:bold}
.cs{width:100%;height:38px;border-radius:7px;border:1.5px solid #38343a;cursor:pointer;padding:0}
.dacts{display:flex;gap:7px}
.db{flex:1;background:#1c1820;border:1.5px solid #38343a;border-radius:9px;
  color:#c0bab8;font-family:'Courier New',monospace;font-size:11px;
  font-weight:bold;padding:11px 4px;cursor:pointer;transition:all .12s}
.db:active{transform:scale(.95);background:#281838}
.db.hi{border-color:#c96a3e;color:#c96a3e}
canvas{width:100%;border-radius:8px;border:1.5px solid #38343a;
  touch-action:none;cursor:crosshair;display:block}

/* Monitor panel */
.mwrap{width:100%;max-width:390px;background:#0c1410;border:1.5px solid #1a4828;
  border-radius:12px;padding:12px;display:none;flex-direction:column;gap:8px}
.mwrap.open{display:flex}
.mrow{display:flex;justify-content:space-between;align-items:center;font-size:11px}
.mlbl{color:#6a6058;letter-spacing:1px}
.mval{color:#28b878;font-weight:bold}
.mval.warn{color:#c96a3e}
.mval.off{color:#5a5048}
.mdot{width:8px;height:8px;border-radius:50%;background:#28b878;display:inline-block;margin-right:6px}
.mdot.off{background:#5a5048}
.mdot.warn{background:#c96a3e}

/* WiFi config */
.wwrap{width:100%;max-width:390px;background:#222028;border:1.5px solid #38343a;
  border-radius:12px;padding:12px;display:flex;flex-direction:column;gap:8px}
.wfld{background:#0c1018;border:1.5px solid #1a2820;border-radius:9px;
  color:#e8e4dc;font-family:'Courier New',monospace;font-size:13px;
  padding:10px;outline:none;width:100%}
.wlbl{font-size:10px;color:#8a8278;letter-spacing:1px;font-weight:bold}
.wgo{background:#c96a3e;border:none;border-radius:9px;color:#fff;
  font-family:'Courier New',monospace;font-size:12px;font-weight:bold;
  padding:11px;cursor:pointer;width:100%}
.wgo:active{background:#a04820}
.wstat{font-size:10px;color:#6a6058;text-align:center;line-height:1.5}

/* Toast */
.toast{position:fixed;bottom:18px;left:50%;transform:translateX(-50%);
  background:#252428;border:1.5px solid #38343a;border-radius:9px;
  font-size:12px;color:#d8d4cc;padding:7px 16px;opacity:0;
  transition:opacity .18s;pointer-events:none;white-space:nowrap;z-index:99}
.toast.show{opacity:1}
</style>
</head>
<body>

<div class="hdr">
  <span class="mascot">&#x2590;&#x259B;&#x2588;&#x2588;&#x2588;&#x259C;&#x258C;<br>&#x259C;&#x2588;&#x2588;&#x2588;&#x2588;&#x2588;&#x259B;<br>&#x2598;&#x2598;&nbsp;&#x259D;&#x259D;</span>
  <div class="sitename">CLAWD &middot; MOCHI &middot; CONTROLLER</div>
</div>

<div class="busy" id="busy"><div class="busy-i"></div></div>

<div class="sec">// controls</div>
<div class="ctrl">
  <button class="cbtn on" id="blBtn" onclick="toggleBL()">&#9728; display on</button>
</div>

<div class="sec">// views</div>
<div class="vgrid">
  <button class="vbtn active" data-v="0" onclick="setView(0)">
    <span class="ic">&#9632; &#9632;</span>
    <span class="nm">Normal eyes</span>
    <span class="ht">wiggle + blink</span>
  </button>
  <button class="vbtn" data-v="1" onclick="setView(1)">
    <span class="ic">&gt; &lt;</span>
    <span class="nm">Squish eyes</span>
    <span class="ht">open / close</span>
  </button>
  <button class="vbtn" data-v="2" onclick="setView(2)">
    <span class="ic">{ }</span>
    <span class="nm">Claude Code</span>
    <span class="ht">opens terminal</span>
  </button>
  <button class="vbtn" data-v="3" onclick="toggleCanvas()">
    <span class="ic">&#11035;</span>
    <span class="nm">Canvas</span>
    <span class="ht">draw on display</span>
  </button>
  <button class="vbtn" data-v="4" onclick="setView(4)">
    <span class="ic">&#9679;</span>
    <span class="nm">Monitor</span>
    <span class="ht">Claude Code link</span>
  </button>
</div>

<div class="mwrap" id="mwrap">
  <div class="mrow"><span class="mlbl">MONITOR MODE</span><span class="mval" id="mState">idle</span></div>
  <div class="mrow"><span class="mlbl">DEVICE IP</span><span class="mval" id="mIp">—</span></div>
  <div class="mrow"><span class="mlbl">PC LINK</span><span class="mval" id="mLink"><span class="mdot off" id="mDot"></span><span id="mLinkTxt">waiting</span></span></div>
  <div class="mrow"><span class="mlbl">LAST EVENT</span><span class="mval" id="mLast">—</span></div>
</div>

<div class="sec">// wifi setup</div>
<div class="wwrap">
  <span class="wlbl">HOME WIFI SSID</span>
  <input class="wfld" id="wifiSsid" type="text" placeholder="Your WiFi name" autocomplete="off">
  <span class="wlbl">PASSWORD</span>
  <input class="wfld" id="wifiPass" type="password" placeholder="WiFi password">
  <button class="wgo" onclick="saveWifi()">Save &amp; Connect</button>
  <div class="wstat" id="wifiStat">AP always on: ClaWD-Mochi / clawd1234</div>
</div>

<div class="sec">// speed</div>
<div class="speed-row">
  <span class="sl">slow</span>
  <input type="range" id="spd" min="1" max="3" value="2" step="1" oninput="setSpeed(this.value)">
  <span class="sv" id="spdV">normal</span>
</div>

<div class="ctrl">
  <div class="ci" style="flex:1;display:flex;flex-direction:column;gap:4px;align-items:stretch">
    <span class="cl" style="font-size:10px;color:#8a8278;letter-spacing:1px;font-weight:bold;text-align:center">BACKGROUND</span>
    <input type="color" class="cs" id="bgCol" value="#aa4818" oninput="onBgChange(this.value)">
  </div>
  <div class="ci" style="flex:1;display:flex;flex-direction:column;gap:4px;align-items:stretch">
    <span class="cl" style="font-size:10px;color:#8a8278;letter-spacing:1px;font-weight:bold;text-align:center">PEN COLOR</span>
    <input type="color" class="cs" id="penCol" value="#000000">
  </div>
</div>

<div class="sec">// terminal</div>
<div class="twrap" id="twrap">
  <div class="thdr">
    <span class="tttl">&#9658; clawd:~$</span>
    <button class="tx" onclick="closeTerm()">&#x2715; exit terminal</button>
  </div>
  <div class="trow">
    <input class="tin" id="tin" type="text" placeholder="type here..."
           autocomplete="off" autocorrect="off" autocapitalize="off" spellcheck="false">
    <button class="tgo" onclick="termEnter()">&#8629;</button>
  </div>
</div>

<div class="cwrap" id="cwrap">
  <div class="dacts">
    <button class="db hi" onclick="clearAll()">&#11035; clear</button>
    <button class="db" style="border-color:#28b878;color:#28b878" onclick="toggleCanvas()">&#10003; done</button>
  </div>
  <canvas id="cvs" width="240" height="240"></canvas>
</div>

<div class="toast" id="toast"></div>

<script>
let activeView  = 0;
let termOpen    = false;
let canvasOpen  = false;
let blOn        = true;
let isBusy      = false;
let drawing     = false;
let lastX = 0, lastY = 0;
let tt;
let monitorPoll = null;

const spdLabels = ['','slow','normal','fast'];
const mstateLabels = {idle:'idle',thinking:'thinking',working:'working',alert:'alert',offline:'offline'};

// ── Toast ──────────────────────────────────────────────────────
function toast(msg, ok=true) {
  const el = document.getElementById('toast');
  el.textContent = msg;
  el.style.borderColor = ok ? '#28b878' : '#c96a3e';
  el.classList.add('show');
  clearTimeout(tt);
  tt = setTimeout(() => el.classList.remove('show'), 1300);
}

// ── Busy ────────────────────────────────────────────────────────
function setBusy(b) {
  isBusy = b;
  document.getElementById('busy').classList.toggle('show', b);
  const locked = b || termOpen;
  document.querySelectorAll('.vbtn').forEach(el => {
    // when canvas open, keep canvas btn (data-v=3) active so user can exit
    el.disabled = canvasOpen ? parseInt(el.dataset.v) !== 3 : locked;
  });
  document.querySelectorAll('.lbtn').forEach(el => el.disabled = locked || canvasOpen);
  document.querySelectorAll('.cbtn').forEach(el => {
    if (el.id !== 'blBtn') el.disabled = locked;
  });
}

// ── HTTP ────────────────────────────────────────────────────────
async function req(path) {
  try { const r = await fetch(path); return r.ok; }
  catch(e) { toast('no connection', false); return false; }
}

async function waitNotBusy() {
  for (let i = 0; i < 100; i++) {
    try {
      const r = await fetch('/state');
      const j = await r.json();
      if (!j.busy) return;
    } catch(e) {}
    await new Promise(r => setTimeout(r, 150));
  }
}

// ── Background colour ───────────────────────────────────────────
async function onBgChange(hex) {
  if (canvasOpen) {
    await req('/draw/clear?bg=' + encodeURIComponent(hex));
  } else {
    await req('/redraw?bg=' + encodeURIComponent(hex));
  }
  redrawCanvas(hex);
}

// ── Speed ───────────────────────────────────────────────────────
async function setSpeed(v) {
  document.getElementById('spdV').textContent = spdLabels[v];
  await req('/speed?v=' + v);
}

// ── Views ───────────────────────────────────────────────────────
async function setView(v) {
  if (isBusy || termOpen || canvasOpen) return;
  if (v === 3) { toggleCanvas(); return; }
  const keys = ['w','s','d','m'];
  if (v >= 0 && v <= 2) {
    if (!await req('/cmd?k=' + keys[v])) return;
  } else if (v === 4) {
    if (!await req('/cmd?k=m')) return;
  } else return;

  activeView = v;
  document.querySelectorAll('.vbtn').forEach(b =>
    b.classList.toggle('active', parseInt(b.dataset.v) === v));
  document.getElementById('mwrap').classList.toggle('open', v === 4);

  if (v === 4) {
    startMonitorPoll();
    toast('monitor mode');
    return;
  }
  stopMonitorPoll();

  if (v === 2) {
    termOpen = true;
    document.getElementById('twrap').classList.add('open');
    setBusy(false);
    setBusy(false);
    document.querySelectorAll('.vbtn,.lbtn').forEach(b => b.disabled = true);
    const cvb = document.getElementById('cvBtn'); if (cvb) cvb.disabled = true;
    document.getElementById('tin').focus();
    toast('terminal open');
    return;
  }
  setBusy(true);
  await waitNotBusy();
  setBusy(false);
}

// ── Monitor status poll ─────────────────────────────────────────
function updateMonitorUI(j) {
  const st = j.mstate || 'idle';
  document.getElementById('mState').textContent = st;
  document.getElementById('mIp').textContent = j.sta_ip || (j.sta ? 'connecting...' : 'not on home WiFi');
  const ago = j.last_status || 0;
  const dot = document.getElementById('mDot');
  const linkTxt = document.getElementById('mLinkTxt');
  const lastEl = document.getElementById('mLast');
  if (ago > 0 && ago < 35) {
    dot.className = 'mdot';
    linkTxt.textContent = 'connected (' + ago + 's ago)';
    lastEl.textContent = st + ' · ' + ago + 's ago';
  } else if (ago >= 35) {
    dot.className = 'mdot warn';
    linkTxt.textContent = 'stale (' + ago + 's)';
    lastEl.textContent = 'timeout → idle';
  } else {
    dot.className = 'mdot off';
    linkTxt.textContent = 'waiting for hook';
    lastEl.textContent = '—';
  }
}

function startMonitorPoll() {
  stopMonitorPoll();
  async function poll() {
    try {
      const r = await fetch('/state');
      const j = await r.json();
      updateMonitorUI(j);
    } catch(e) {}
  }
  poll();
  monitorPoll = setInterval(poll, 2000);
}

function stopMonitorPoll() {
  if (monitorPoll) { clearInterval(monitorPoll); monitorPoll = null; }
}

// ── WiFi config ─────────────────────────────────────────────────
async function saveWifi() {
  const ssid = document.getElementById('wifiSsid').value.trim();
  const pass = document.getElementById('wifiPass').value;
  if (!ssid) { toast('enter SSID', false); return; }
  document.getElementById('wifiStat').textContent = 'Connecting...';
  try {
    const r = await fetch('/wifi/save?ssid=' + encodeURIComponent(ssid) +
      '&pass=' + encodeURIComponent(pass));
    const j = await r.json();
    if (j.ok && j.sta) {
      document.getElementById('wifiStat').textContent = 'Connected: ' + j.sta_ip;
      toast('WiFi connected');
    } else {
      document.getElementById('wifiStat').textContent = 'Failed — check SSID/password';
      toast('WiFi failed', false);
    }
  } catch(e) {
    document.getElementById('wifiStat').textContent = 'Request failed';
    toast('no connection', false);
  }
}

// ── Logo animations (kept for startup, not exposed in UI) ──────

// ── Backlight ───────────────────────────────────────────────────
async function toggleBL() {
  blOn = !blOn;
  await req('/backlight?on=' + (blOn ? 1 : 0));
  const b = document.getElementById('blBtn');
  b.textContent = blOn ? '\u2600 display on' : '\u25cb display off';
  b.classList.toggle('on', blOn);
  b.classList.toggle('dim', !blOn);
}

// ── Canvas toggle ───────────────────────────────────────────────
async function toggleCanvas() {
  canvasOpen = !canvasOpen;
  document.getElementById('cwrap').classList.toggle('open', canvasOpen);
  const b = document.getElementById('cvBtn');
  if (b) { b.classList.toggle('on', canvasOpen); b.textContent = canvasOpen ? '\u2b1b canvas on' : '\u2b1b canvas'; }
  document.querySelectorAll('.vbtn').forEach(btn =>
    btn.classList.toggle('active', canvasOpen && parseInt(btn.dataset.v) === 3));
  if (canvasOpen) stopMonitorPoll();
  document.getElementById('mwrap').classList.remove('open');
  await req('/canvas?on=' + (canvasOpen ? 1 : 0));
  if (canvasOpen) {
    const bg = document.getElementById('bgCol').value;
    redrawCanvas(bg);
    await req('/draw/clear?bg=' + encodeURIComponent(bg));
    // lock all other buttons
    document.querySelectorAll('.vbtn,.lbtn').forEach(b => b.disabled = true);
    toast('canvas active');
  } else {
    setBusy(false);   // re-evaluate locks
    toast('canvas off');
  }
}

// ── Terminal ────────────────────────────────────────────────────
const tin = document.getElementById('tin');
let lastVal = '';
tin.addEventListener('input', async () => {
  const cur = tin.value, prev = lastVal;
  if (cur.length > prev.length) {
    await req('/char?c=' + encodeURIComponent(cur[cur.length - 1]));
  } else if (cur.length < prev.length) {
    await req('/char?c=%08');
  }
  lastVal = cur;
});
async function termEnter() {
  await req('/char?c=%0A');
  tin.value = ''; lastVal = ''; tin.focus();
}
tin.addEventListener('keydown', e => {
  if (e.key === 'Enter') { e.preventDefault(); termEnter(); }
});
async function closeTerm() {
  await req('/cmd?k=q');
  termOpen = false;
  document.getElementById('twrap').classList.remove('open');
  setBusy(false);
  toast('terminal closed');
}

// ── Canvas drawing — send full stroke on finger lift ────────────
const cvs = document.getElementById('cvs');
const ctx = cvs.getContext('2d');
let strokePts = [];

function getPos(e) {
  const r = cvs.getBoundingClientRect();
  const sx = cvs.width / r.width, sy = cvs.height / r.height;
  const s = e.touches ? e.touches[0] : e;
  return { x: (s.clientX - r.left) * sx, y: (s.clientY - r.top) * sy };
}

function redrawCanvas(hex) {
  ctx.fillStyle = hex;
  ctx.fillRect(0, 0, cvs.width, cvs.height);
}

function startDraw(e) {
  e.preventDefault();
  drawing = true;
  strokePts = [];
  const p = getPos(e); lastX = p.x; lastY = p.y;
  strokePts.push({ x: Math.round(p.x), y: Math.round(p.y) });
  // draw dot on canvas preview only — no display send yet
  ctx.beginPath(); ctx.arc(p.x, p.y, 2, 0, Math.PI * 2);
  ctx.fillStyle = document.getElementById('penCol').value; ctx.fill();
}
function moveDraw(e) {
  if (!drawing) return; e.preventDefault();
  const p = getPos(e);
  ctx.beginPath(); ctx.moveTo(lastX, lastY); ctx.lineTo(p.x, p.y);
  ctx.strokeStyle = document.getElementById('penCol').value;
  ctx.lineWidth = 4; ctx.lineCap = 'round'; ctx.stroke();
  strokePts.push({ x: Math.round(p.x), y: Math.round(p.y) });
  lastX = p.x; lastY = p.y;
}
async function endDraw(e) {
  if (!drawing) return; drawing = false;
  if (!canvasOpen || strokePts.length < 1) return;
  const pen = document.getElementById('penCol').value.replace('#', '');
  const pts = strokePts.map(p => p.x + ',' + p.y).join(';');
  await req('/draw/stroke?pen=' + pen + '&pts=' + encodeURIComponent(pts));
  strokePts = [];
}

cvs.addEventListener('mousedown',  startDraw);
cvs.addEventListener('mousemove',  moveDraw);
cvs.addEventListener('mouseup',    endDraw);
cvs.addEventListener('mouseleave', endDraw);
cvs.addEventListener('touchstart', startDraw, {passive:false});
cvs.addEventListener('touchmove',  moveDraw,  {passive:false});
cvs.addEventListener('touchend',   endDraw);

// Clear = clear both web canvas and display
async function clearAll() {
  const bg = document.getElementById('bgCol').value;
  redrawCanvas(bg);
  await req('/draw/clear?bg=' + encodeURIComponent(bg));
  toast('cleared');
}

// Init: sync speed and backlight from ESP32, reset bg to default
(async () => {
  try {
    const r = await fetch('/state');
    const j = await r.json();
    const spd = j.speed || 2;
    document.getElementById('spd').value = spd;
    document.getElementById('spdV').textContent = spdLabels[spd];
    if (j.bl === false) {
      blOn = false;
      const b = document.getElementById('blBtn');
      b.textContent = '\u25cb display off';
      b.classList.remove('on'); b.classList.add('dim');
    }
    if (j.sta_ip) {
      document.getElementById('wifiStat').textContent =
        'Home WiFi: ' + j.sta_ip + ' · AP: ClaWD-Mochi';
    }
    updateMonitorUI(j);
  } catch(e) {}
  document.getElementById('bgCol').value = '#aa4818';
  redrawCanvas('#aa4818');
})();
</script>
</body>
</html>
)rawhtml";

// ═════════════════════════════════════════════════════════════
//  WEB ROUTES
// ═════════════════════════════════════════════════════════════

void routeRoot() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.send_P(200, "text/html", INDEX_HTML);
}

void routeCmd() {
  if (!server.hasArg("k") || server.arg("k").isEmpty()) {
    server.send(400, "application/json", "{\"e\":1}"); return;
  }
  const char c = server.arg("k")[0];

  if (termMode) {
    if (c == 'q') { termMode = false; drawCodeView(); }
    server.send(200, "application/json", "{\"ok\":1}"); return;
  }

  server.send(200, "application/json", "{\"ok\":1}");
  switch (c) {
    case 'w': currentView = VIEW_EYES_NORMAL; animNormalEyes(); break;
    case 's': currentView = VIEW_EYES_SQUISH; animSquishEyes(); break;
    case 'd':
      currentView = VIEW_CODE; drawCodeView();
      termMode = true; termClear(); termFullRedraw(); break;
    case 'm':
      enterMonitorView();
      break;
    case 'a':
      currentView = VIEW_EYES_NORMAL;
      animLogoReveal();
      break;
  }
}

void routeChar() {
  if (!termMode) { server.send(200, "application/json", "{\"ok\":1}"); return; }
  const String val = server.arg("c");
  if (val.length() > 0) termAddChar(val[0]);
  server.send(200, "application/json", "{\"ok\":1}");
}

void routeSpeed() {
  if (server.hasArg("v")) animSpeed = constrain(server.arg("v").toInt(), 1, 3);
  server.send(200, "application/json", "{\"ok\":1}");
}

// /redraw?bg=hex — set animBg and immediately redraw current view
void routeRedraw() {
  if (server.hasArg("bg")) {
    animBgColor = hexToRgb565(server.arg("bg"));
    drawBgColor = animBgColor;
  }
  switch (currentView) {
    case VIEW_EYES_NORMAL: drawNormalEyes(); break;
    case VIEW_EYES_SQUISH: drawSquishEyes(); break;
    case VIEW_CODE:        drawCodeView();   break;
    case VIEW_DRAW:        tft.fillScreen(drawBgColor); break;
    case VIEW_MONITOR:
      switch (monitorState) {
        case MON_ALERT:
          drawLogoFilled(animBgColor, C_WHITE);   // 重绘 ALERT 定格画面(animLogoReveal 末行)
          break;
        case MON_OFFLINE:
          drawNormalEyes();
          break;
        default:
          rigApplyExpression(true);   // 换背景色后整区重绘
          break;
      }
      break;
  }
  server.send(200, "application/json", "{\"ok\":1}");
}

void routeCanvas() {
  const bool on = server.hasArg("on") && server.arg("on") == "1";
  if (on) { currentView = VIEW_DRAW; tft.fillScreen(drawBgColor); }
  server.send(200, "application/json", "{\"ok\":1}");
}

void routeDrawClear() {
  const String bg = server.hasArg("bg") ? server.arg("bg") : "#aa4818";
  drawBgColor = hexToRgb565(bg);
  animBgColor = drawBgColor;  // keep in sync
  currentView = VIEW_DRAW; termMode = false;
  tft.fillScreen(drawBgColor);
  server.send(200, "application/json", "{\"ok\":1}");
}

void routeDrawStroke() {
  if (!server.hasArg("pts") || !server.hasArg("pen")) {
    server.send(200, "application/json", "{\"ok\":1}"); return;
  }
  const uint16_t color = hexToRgb565(server.arg("pen"));
  const String   data  = server.arg("pts");
  currentView = VIEW_DRAW;

  struct Pt { int16_t x, y; };
  Pt prev = {-1, -1};
  int start = 0;
  while (start < (int)data.length()) {
    int semi = data.indexOf(';', start);
    if (semi == -1) semi = data.length();
    String entry = data.substring(start, semi);
    const int comma = entry.indexOf(',');
    if (comma > 0) {
      const int16_t x = entry.substring(0, comma).toInt();
      const int16_t y = entry.substring(comma + 1).toInt();
      if (prev.x >= 0) {
        tft.drawLine(prev.x, prev.y, x, y, color);
        tft.drawLine(prev.x + 1, prev.y, x + 1, y, color);
        tft.drawLine(prev.x, prev.y + 1, x, y + 1, color);
      } else {
        tft.fillCircle(x, y, 2, color);
      }
      prev = {x, y};
    }
    start = semi + 1;
  }
  server.send(200, "application/json", "{\"ok\":1}");
}

void routeStatus() {
  if (!server.hasArg("s") || server.arg("s").isEmpty()) {
    server.send(400, "application/json", "{\"e\":1}");
    return;
  }
  const String s = server.arg("s");
  server.send(200, "application/json", "{\"ok\":1}");
  applyMonitorState(s);
}

void routeWifiSave() {
  if (!server.hasArg("ssid") || server.arg("ssid").isEmpty()) {
    server.send(400, "application/json", "{\"e\":1}");
    return;
  }
  const String ssid = server.arg("ssid");
  const String pass = server.hasArg("pass") ? server.arg("pass") : "";
  saveWifiCredentials(ssid, pass);
  WiFi.disconnect();
  delay(100);
  connectSTA();
  drawWifiScreen();

  String j = "{\"ok\":1,\"sta\":";
  j += staConnected ? "true" : "false";
  j += ",\"sta_ip\":\"";
  j += staIP;
  j += "\"}";
  server.send(200, "application/json", j);
}

void routeBacklight() {
  setBacklight(server.hasArg("on") && server.arg("on") == "1");
  server.send(200, "application/json", "{\"ok\":1}");
}

// Convert RGB565 back to #RRGGBB for state endpoint
String rgb565ToHex(uint16_t c) {
  uint8_t r = ((c >> 11) & 0x1F) << 3;
  uint8_t g = ((c >> 5)  & 0x3F) << 2;
  uint8_t b = (c & 0x1F) << 3;
  char buf[8];
  snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
  return String(buf);
}

void routeState() {
  String j = "{\"view\":"; j += currentView;
  j += ",\"busy\":";   j += busy        ? "true" : "false";
  j += ",\"term\":";   j += termMode    ? "true" : "false";
  j += ",\"bl\":";     j += backlightOn ? "true" : "false";
  j += ",\"speed\":";  j += animSpeed;
  j += ",\"mstate\":\""; j += monitorStateStr(); j += "\"";
  j += ",\"sta\":";    j += staConnected ? "true" : "false";
  j += ",\"sta_ip\":\""; j += staIP; j += "\"";
  j += ",\"monitor\":"; j += (currentView == VIEW_MONITOR) ? "true" : "false";
  j += ",\"last_status\":";
  j += (lastStatusMs == 0) ? 0 : (int)((millis() - lastStatusMs) / 1000);
  j += "}";
  server.send(200, "application/json", j);
}

void routeNotFound() { server.send(404, "text/plain", "not found"); }

// ═════════════════════════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());

  pinMode(TFT_BLK, OUTPUT);
  setBacklight(true);

  SPI.begin(8, -1, 9, TFT_CS);    // SCK=8, MOSI=9
  tft.init(240, 240);
  tft.setSPISpeed(62000000);
  tft.setRotation(1);
  initColours();

  // ── Boot splash ────────────────────────────────────────────
  tft.fillScreen(animBgColor);
  tft.setTextColor(C_WHITE); tft.setTextSize(3);
  tft.setCursor(DISP_W / 2 - 54, DISP_H / 2 - 22); tft.print("Clawd");
  tft.setCursor(DISP_W / 2 - 54, DISP_H / 2 + 14); tft.print("Mochi");
  delay(1200);

  // ── Logo shown once at startup ─────────────────────────────
  animLogoReveal();

  // ── Start WiFi (AP + optional STA) ─────────────────────────
  prefs.begin("clawd", false);
  loadWifiCredentials();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);

  if (savedSSID.length() > 0) {
    connectSTA();
  }

  drawWifiScreen();

  // ── Register routes ────────────────────────────────────────
  server.on("/",            HTTP_GET, routeRoot);
  server.on("/cmd",         HTTP_GET, routeCmd);
  server.on("/char",        HTTP_GET, routeChar);
  server.on("/speed",       HTTP_GET, routeSpeed);
  server.on("/redraw",      HTTP_GET, routeRedraw);
  server.on("/canvas",      HTTP_GET, routeCanvas);
  server.on("/draw/clear",  HTTP_GET, routeDrawClear);
  server.on("/draw/stroke", HTTP_GET, routeDrawStroke);
  server.on("/backlight",   HTTP_GET, routeBacklight);
  server.on("/status",      HTTP_GET, routeStatus);
  server.on("/wifi/save",   HTTP_GET, routeWifiSave);
  server.on("/state",       HTTP_GET, routeState);
  server.onNotFound(routeNotFound);
  server.begin();
}

// ═════════════════════════════════════════════════════════════
//  LOOP
// ═════════════════════════════════════════════════════════════

void loop() {
  server.handleClient();
  checkStatusTimeout();
  checkIdleRotation();
  tickMonitorAnimation();

  static unsigned long lastWifiCheck = 0;
  if (millis() - lastWifiCheck > 30000) {
    lastWifiCheck = millis();
    if (savedSSID.length() > 0) {
      const bool wasConnected = staConnected;
      staConnected = (WiFi.status() == WL_CONNECTED);
      if (staConnected) {
        staIP = WiFi.localIP().toString();
      } else {
        staIP = "";
        if (wasConnected) connectSTA();
      }
    }
  }
}
