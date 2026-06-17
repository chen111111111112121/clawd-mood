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
 *   AP:  "ClaWD-Mood"  pw: clawd1234  → http://192.168.4.1
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
#include <ESPmDNS.h>
#include <Preferences.h>

// ── Pins ──────────────────────────────────────────────────────
#define TFT_CS  4
#define TFT_DC  1
#define TFT_RST 2
#define TFT_BLK 3

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// ── WiFi ──────────────────────────────────────────────────────
const char* AP_SSID = "ClaWD-Mood";
const char* AP_PASS = "clawd1234";
WebServer server(80);
Preferences prefs;

bool     staConnected = false;
String   staIP        = "";
String   savedSSID    = "";
String   savedPASS    = "";

#define STA_CONNECT_TIMEOUT_MS 10000
#define STATUS_TIMEOUT_MS      180000   // thinking/working 无新事件多久自动回 idle;放宽到 3min 以容纳长思考/长回答(纯生成阶段宿主不发事件)

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
uint16_t C_ORANGE, C_DARKBG, C_MUTED, C_GREEN, C_BLUSH;
#define C_WHITE ST77XX_WHITE
#define C_BLACK ST77XX_BLACK

// ── State ─────────────────────────────────────────────────────
#define VIEW_MONITOR     4
#define VIEW_BOOTINFO    5

#define MON_IDLE     0
#define MON_THINKING 1
#define MON_WORKING  2
#define MON_ALERT    3
#define MON_OFFLINE  4

#define IDLE_NORMAL    0
#define IDLE_SLEEPY    1
#define IDLE_HEART     2
#define IDLE_HAPPY     3
#define IDLE_CURIOUS   4   // 好奇张望 + ?
#define IDLE_WINK      5   // 俏皮眨眼(右眼闭) + 小嘴
#define IDLE_SPARKLE   6   // 星星眼 + 闪光
#define IDLE_SURPRISED 7   // 瞪大 + !
#define IDLE_SHY       8   // 害羞 + 腮红
#define IDLE_DIZZY     9   // 转圈 + 头顶星
#define IDLE_MUSIC     10  // 摇摆 + 音符
#define IDLE_YAWN      11  // 打哈欠 + O 嘴
#define IDLE_LOVE      12  // 花痴 + 飘心
#define IDLE_GIGGLE    13  // 偷笑 + 笑纹

#define SCAN_EYE_W   20
#define IDLE_SWITCH_MS 30000   // 空闲表情每 30 秒随机切换一次

const uint8_t IDLE_POOL[] = {
  IDLE_NORMAL, IDLE_SLEEPY, IDLE_HEART, IDLE_HAPPY,
  IDLE_CURIOUS, IDLE_WINK, IDLE_SPARKLE, IDLE_SURPRISED, IDLE_SHY,
  IDLE_DIZZY, IDLE_MUSIC, IDLE_YAWN, IDLE_LOVE, IDLE_GIGGLE
};
#define IDLE_POOL_SIZE 14

// ── 情绪引擎:4 种治愈系心情,各圈定一组 idle 表情 ──────────────
// 心情只在 idle 体现:按当前心情在对应子集里轮播。energy(工作降/空闲升)+joy(done加分/衰减)判定。
#define MOOD_FOCUSED  0   // 专注:正常工作节奏
#define MOOD_TIRED    1   // 疲惫:长时间连续干活
#define MOOD_CHEERFUL 2   // 雀跃:刚连续完成几件事
#define MOOD_COZY     3   // 惬意:放松的默认态(空闲)
const uint8_t MOOD_FOCUSED_POOL[]  = { IDLE_NORMAL, IDLE_CURIOUS, IDLE_WINK };               // 普通/好奇/眨眼
const uint8_t MOOD_TIRED_POOL[]    = { IDLE_SLEEPY, IDLE_YAWN, IDLE_DIZZY };                 // 困倦/哈欠/转圈
const uint8_t MOOD_CHEERFUL_POOL[] = { IDLE_HAPPY, IDLE_SPARKLE, IDLE_GIGGLE, IDLE_MUSIC, IDLE_SURPRISED }; // 开心/星星眼/偷笑/听歌/惊讶
const uint8_t MOOD_COZY_POOL[]     = { IDLE_HEART, IDLE_LOVE, IDLE_GIGGLE, IDLE_MUSIC, IDLE_SHY };         // 爱心/花痴/偷笑/听歌/害羞

// 情绪累积速率(每分钟)与阈值,均可调
#define ENERGY_DRAIN_PER_MIN   2.5f   // 工作/思考:~28min 降到疲惫阈值
#define ENERGY_RECOVER_PER_MIN 7.0f   // 空闲:回血更快
#define JOY_DECAY_PER_MIN      4.0f
#define JOY_PER_DONE           30
#define MOOD_CHEERFUL_JOY      50      // joy≥此值→雀跃(需连续~2次 done)
#define MOOD_TIRED_ENERGY      30      // energy≤此值→疲惫
#define MOOD_FOCUSED_WINDOW_MS 180000UL  // 最近3分钟有活动→专注

// ── 睡眠节律:困意累积(idle 涨/活动清零)到阈值后触发入睡脚本 ──────
#define SLEEPINESS_PER_MIN     30.0f    // 基准困意速率(精力中等);测试可临时调 60 加速
#define SLEEPINESS_EF_HI       0.6f    // energy=100 时速率系数(精神,扛困)
#define SLEEPINESS_EF_LO       1.6f    // energy=0   时速率系数(累瘫,秒睡)
#define SLEEP_DROWSY_AT        25.0f   // 困意≥:触发入睡脚本(先打哈欠再入睡) ~5min
#define ENERGY_SLEEP_PER_MIN   18.0f   // 睡眠期精力快速回血(睡越久越精神)

// 入睡脚本时间线(从触发起算的毫秒;自然曲线见 web/mockups/falling-asleep.html)
#define SLP_YAWN_DUR  1800UL                    // 单次哈欠时长(多次穿插,见 SLEEP_YAWNS)
#define SLP_CLOSE_AT  16500UL                   // 合眼完成→睡着(Zzz)
#define SLP_DEEP_AT   18000UL                   // →熟睡(鼻涕泡)

// ── 清醒 idle 轮播:以普通表情为中心 ─────────────────────────────
#define NORMAL_HOLD_MS         10000UL // 普通表情每次至少持续
#define OTHER_LOOP_COUNT       3       // 其他表情循环次数

// 睡眠阶段(活在 MON_IDLE 内部)
#define SLEEP_AWAKE   0
#define SLEEP_DROWSY  1
#define SLEEP_ASLEEP  2
#define SLEEP_DEEP    3

// ── Working sub-acts (semantic working states) ───────────────
#define ACT_WORK  0
#define ACT_READ  1
#define ACT_EDIT  2
#define ACT_RUN   3
#define ACT_NET   4
#define ACT_AGENT 5

uint8_t workAct = ACT_WORK;
char    tickerText[32] = "";

char    tickerDrawn[32] = "";
uint8_t tickerScroll = 0;
unsigned long tickerScrollMs = 0;
bool    tickerVisible = false;
#define TICKER_Y    213   // 紧贴表情区下沿(28+185),不留无人缝隙
#define TICKER_H    (DISP_H - TICKER_Y)
#define TICKER_COLS 19

uint8_t  currentView   = VIEW_MONITOR;
uint8_t  monitorState  = MON_IDLE;
uint8_t  currentIdleIndex = 0;
uint8_t  currentIdleExpr  = IDLE_NORMAL;
unsigned long lastStatusMs = 0;
unsigned long lastIdleSwitch = 0;

// 情绪引擎状态
uint8_t  currentMood   = MOOD_COZY;
float    moodEnergy    = 80.0f;     // 0..100
float    moodJoy       = 0.0f;      // 0..100
unsigned long lastMoodTick  = 0;    // 上次情绪更新
unsigned long lastActiveMs  = 0;    // 上次 thinking/working 的时刻(判"最近有活动")
float    moodSleepiness = 0.0f;     // 0..100,idle 累积,任何活动状态清零
uint8_t  sleepStage     = SLEEP_AWAKE;
unsigned long sleepScriptMs = 0;    // 入睡脚本锚点:0=清醒 >0=入睡中/睡眠中
bool     sleepClosed   = false;     // 是否已合眼(切到闭眼弧)
bool     sleepInited   = false;     // 入睡脚本首帧是否已初始化
uint8_t  wakeFlourish   = 0;        // 唤醒花絮:0=无 1=温柔 2=惊醒
unsigned long wakeFlourishMs = 0;   // 花絮起始时刻
bool     idleShowingOther = false;  // 清醒轮播子状态:false=普通 hub,true=穿插其他
unsigned long idlePhaseMs = 0;      // 当前段(普通/其他)起始时刻
uint8_t  idleOtherLoops   = 0;      // 当前其他表情已循环次数

#define BOOT_CONFIRM_MS 3000   // 已连上家庭 WiFi:显示 IP 确认 3s 后进表情
unsigned long bootScreenDeadline = 0;   // >0 = 信息屏确认倒计时中(仅"已连上"用);0 = 常驻
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
uint8_t lastTickHeartScale   = 255;
int16_t lastSleepyTop        = -999;
uint8_t lastSleepyZ          = 255;
bool     statusTimedOut  = false;
bool     busy         = false;
bool     backlightOn  = true;
uint8_t  animSpeed    = 2;   // 1=slow 2=normal(default) 3=fast

uint16_t animBgColor  = 0;   // background for eye/logo animations

// ── Eye rig: lively expression engine ────────────────────────
enum EyeStyle : uint8_t { STYLE_RECT, STYLE_CHEVRON, STYLE_ARC, STYLE_HEART, STYLE_STAR, STYLE_SLEEP };

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
bool          idleRightWink = false;   // WINK 表情:右眼当前是否闭合(由行为脚本驱动)

#define RIG_TICK_MS  33
#define RIG_DAMP     196       // 速度阻尼(/256)
#define BLINK_FRAMES 7
static const uint8_t BLINK_H_PCT[BLINK_FRAMES] = {106, 60, 8, 8, 70, 104, 100};
static const uint8_t BLINK_W_PCT[BLINK_FRAMES] = { 97, 105, 118, 118, 103, 98, 100};
static const int8_t  BREATH_TAB[16] = {0,1,1,2,2,2,1,1,0,-1,-1,-2,-2,-2,-1,-1};

// 打字机 edit 表情参数
#define EDIT_COLS    17    // 每行格数
#define EDIT_CELL_W  12    // 格宽(8px 块 + 4px 间隙)
#define EDIT_LINE_X  18    // 行起点 x
#define EDIT_LINE_Y  196   // 当前行 y
#define EDIT_LINE_H  10    // 当前行块高
#define EDIT_TRACE_Y 184   // 上一行痕迹 y
#define EDIT_CARET_H 14    // 光标高
#define EDIT_BAND_H  ((EDIT_LINE_Y + EDIT_CARET_H) - EDIT_TRACE_Y)   // 书写带总高

// 姿态预设
const EyePose POSE_NORMAL = {STYLE_RECT,    0,  0, EYE_W, EYE_H, 0};
const EyePose POSE_SLEEPY = {STYLE_RECT,    0,  8, EYE_W, EYE_H, 170};
const EyePose POSE_HEART  = {STYLE_HEART,   0,  0, 6, 6, 0};
const EyePose POSE_HAPPY  = {STYLE_ARC,     0,  8, 30, 30, 0};
const EyePose POSE_THINK  = {STYLE_RECT,    0, 10, EYE_W, 30, 0};   // 微眯下垂平眼,配上方省略号(thinkingDotsOverlay)
const EyePose POSE_SCAN   = {STYLE_RECT,    0,  0, SCAN_EYE_W, EYE_H, 0};
const EyePose POSE_READ   = {STYLE_RECT,    0, 26, 26, 16, 0};
const EyePose POSE_EDIT   = {STYLE_RECT, -13, 12, 20, 34, 40};
const EyePose POSE_RUN    = {STYLE_RECT,    0,  0, 14, 44, 0};
const EyePose POSE_NET    = {STYLE_RECT,    0,  0, EYE_W, EYE_H, 0};
// ── 新空闲表情姿态预设(行为脚本在 rigBehaviorTick,覆盖层在 idleNewOverlay) ──
const EyePose POSE_CURIOUS   = {STYLE_RECT, 0, 0, 28, 54, 0};
const EyePose POSE_WINK      = {STYLE_RECT, 0, 0, EYE_W, 56, 0};
const EyePose POSE_SPARKLE   = {STYLE_STAR, 0, 0, 7, 7, 0};
const EyePose POSE_SURPRISED = {STYLE_RECT, 0, 0, EYE_W, EYE_H, 0};
const EyePose POSE_SHY       = {STYLE_RECT, 0, 6, 24, 42, 30};
const EyePose POSE_DIZZY     = {STYLE_RECT, 0, 0, 26, 40, 60};
const EyePose POSE_MUSIC     = {STYLE_ARC,  0, 8, 30, 30, 0};
const EyePose POSE_YAWN      = {STYLE_RECT, 0, 0, EYE_W, 56, 0};
// ── 睡眠姿态:DOZE/DEEPSLEEP=闭眼弧(⌣);SHUT=唤醒起手的闭眼矩形 ──
const EyePose POSE_DOZE      = {STYLE_SLEEP, 0, 10, EYE_W, 10, 0};        // 睡着:闭眼弧,略下沉
const EyePose POSE_DEEPSLEEP = {STYLE_SLEEP, 0, 16, EYE_W, 10, 0};        // 熟睡:闭眼弧,更下沉
const EyePose POSE_SHUT      = {STYLE_RECT,  0,  6, EYE_W, EYE_H, 235};   // 唤醒起手:闭眼矩形(同样式平滑睁开)
const EyePose POSE_LOVE      = {STYLE_HEART, 0, 0, 6, 6, 0};
const EyePose POSE_GIGGLE    = {STYLE_ARC,  0, 8, 30, 30, 0};

// ═════════════════════════════════════════════════════════════
//  HELPERS
// ═════════════════════════════════════════════════════════════

int speedMs(int ms) {
  if (animSpeed == 3) return ms / 2;
  if (animSpeed == 1) return ms * 2;
  return ms;
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
  C_BLUSH  = tft.color565(255, 92, 120);   // 害羞腮红(粉)
  animBgColor = C_ORANGE;
}

// ═════════════════════════════════════════════════════════════
//  WIFI + MONITOR
// ═════════════════════════════════════════════════════════════

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
  WiFi.setAutoReconnect(true);    // 本次主动连接,允许连上后自动恢复
  WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < STA_CONNECT_TIMEOUT_MS) {
    delay(200);
  }
  staConnected = (WiFi.status() == WL_CONNECTED);
  staIP = staConnected ? WiFi.localIP().toString() : "";
  if (!staConnected) {
    // 连不上:关掉 STA 后台自动重连并断开。单射频下 AP+STA 必须同信道,
    // 若 STA 持续重试一个够不到的网络,会不断搅乱 softAP,导致手机扫不到配网热点。
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);  // 仅停 STA,保留射频给 AP
  }
  return staConnected;
}

void startMDNS() {
  if (!staConnected) return;
  MDNS.end();
  if (MDNS.begin("clawd")) {              // http://clawd.local
    MDNS.addService("http", "tcp", 80);
  }
}

void drawWifiScreen(uint32_t autoLeaveMs) {
  tft.fillScreen(C_DARKBG);
  tft.fillRect(0, 0, DISP_W, 4, C_ORANGE);
  tft.setTextColor(C_WHITE); tft.setTextSize(2);
  tft.setCursor(12, 16);  tft.print("WiFi: ClaWD-Mood");
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
    tft.setTextColor(C_GREEN); tft.setTextSize(1);
    tft.setCursor(12, 148); tft.print("or  http://clawd.local");
    tft.setTextColor(C_MUTED); tft.setTextSize(1);
    tft.setCursor(12, 162); tft.print("Hook: /status?s=...");
  } else {
    tft.setTextColor(C_MUTED); tft.setTextSize(1);
    tft.setCursor(12, 124); tft.print("not connected");
    tft.setCursor(12, 140); tft.print("configure in web portal");
  }
  tft.setTextColor(C_MUTED); tft.setTextSize(1);
  tft.setCursor(12, 210);
  if (autoLeaveMs) {
    tft.print("auto start in "); tft.print(autoLeaveMs / 1000); tft.print("s ...");
  } else if (!staConnected) {
    tft.print("waiting for WiFi setup ...");
  } else {
    tft.print("press any view to start");
  }
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

void ensureFullExpressionBg() {
  const uint8_t key = expressionRenderKey();
  if (key != lastRenderKey) {
    tft.fillScreen(animBgColor);
    lastRenderKey = key;
    prevEyeL.valid = false;
    prevEyeR.valid = false;
    lastTickOx = -999;
    lastTickScanOx = -999;
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


// ── Extended idle / work expressions ───────────────────────────

inline int16_t scanEyeLX(int16_t ox) {
  return (DISP_W - (SCAN_EYE_W * 2 + EYE_GAP)) / 2 + EYE_OX + ox;
}
inline int16_t scanEyeRX(int16_t ox) { return scanEyeLX(ox) + SCAN_EYE_W + EYE_GAP; }

const uint8_t HEART5[5] = { 0b01010, 0b11111, 0b11111, 0b01110, 0b00100 };
const uint8_t STAR7[7]  = { 0b0001000, 0b0011100, 0b1111111, 0b0111110, 0b0011100, 0b0010100, 0b0100010 };

void drawHeartAt(int16_t cx, int16_t cy, uint8_t scale, uint16_t col) {
  for (int8_t row = 0; row < 5; row++)
    for (int8_t c = 0; c < 5; c++)
      if (HEART5[row] & (1 << (4 - c)))
        tft.fillRect(cx + (c - 2) * scale, cy + (row - 2) * scale, scale, scale, col);
}

void drawStarAt(int16_t cx, int16_t cy, uint8_t scale, uint16_t col) {
  for (int8_t row = 0; row < 7; row++)
    for (int8_t c = 0; c < 7; c++)
      if (STAR7[row] & (1 << (6 - c)))
        tft.fillRect(cx + (c - 3) * scale, cy + (row - 3) * scale, scale, scale, col);
}

// 单次整块写入位图眼:在内存(GFXcanvas16)拼好"背景+图形"再一次性推屏,每像素只写一次最终色,
// 彻底消除"先刷背景再画黑"造成的脉动闪烁。n=位图边长(5/7),起点对齐 draw*At 的格点。
void blitBitmapEye(const uint8_t* bmp, uint8_t n, int16_t cx, int16_t cy, uint8_t scale,
                   uint16_t fg, uint16_t bg, EyeRect &out) {
  const int16_t W = (int16_t)n * scale, H = (int16_t)n * scale;
  const int16_t x0 = cx - (n / 2) * scale, y0 = cy - (n / 2) * scale;
  GFXcanvas16 cv(W, H);
  cv.fillScreen(bg);
  for (uint8_t row = 0; row < n; row++)
    for (uint8_t c = 0; c < n; c++)
      if (bmp[row] & (1 << (n - 1 - c)))
        cv.fillRect(c * scale, row * scale, scale, scale, fg);
  tft.drawRGBBitmap(x0, y0, cv.getBuffer(), W, H);
  out = {x0, y0, W, H, true};
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

// col 通常为 C_BLACK;传 animBgColor 时即"按字形精确擦除"(重画同一字形为背景色)。
void drawRigEye(int16_t cx, int16_t cy, int16_t w, int16_t h, int16_t lid,
                bool rightFacing, uint16_t col, EyeRect &out) {
  switch (rig.drawnStyle) {
    case STYLE_SLEEP: {
      // 闭眼:向下弯的弧 ⌣（与 happy 的 ∧ 镜像,读作安睡）
      const int16_t hw = w / 2, dip = 7;
      for (uint8_t t = 0; t < 5; t++) {
        tft.drawLine(cx - hw, cy - dip / 2 + t, cx,      cy + dip + t, col);
        tft.drawLine(cx,      cy + dip + t,     cx + hw, cy - dip / 2 + t, col);
      }
      out = {(int16_t)(cx - hw - 2), (int16_t)(cy - dip / 2 - 2),
             (int16_t)(w + 4), (int16_t)(dip + 12), true};
      break;
    }
    case STYLE_CHEVRON: {
      const int16_t arm = ((h / 2) * (240 - lid)) / 240;
      if (arm <= 3) {
        tft.fillRect(cx - w / 2, cy - 4, w, 8, col);
        out = {(int16_t)(cx - w / 2), (int16_t)(cy - 4), w, 8, true};
      } else {
        drawChevron(cx, cy, arm, w, 10, rightFacing, col);
        out = {(int16_t)(cx - w / 2 - 2), (int16_t)(cy - arm - 12),
               (int16_t)(w + 4), (int16_t)(arm * 2 + 24), true};
      }
      break;
    }
    case STYLE_ARC:
      drawHappyArc(cx, cy, w, col);
      out = {(int16_t)(cx - w / 2), (int16_t)(cy - w / 4 - 2),
             (int16_t)(w + 2), (int16_t)(w / 4 + 8), true};
      break;
    case STYLE_HEART: {
      uint8_t scale = (uint8_t)((w < 3) ? 3 : ((w > 9) ? 9 : w));
      drawHeartAt(cx, cy, scale, col);
      out = {(int16_t)(cx - scale * 3), (int16_t)(cy - scale * 3),
             (int16_t)(scale * 6), (int16_t)(scale * 6), true};
      break;
    }
    case STYLE_STAR: {
      uint8_t scale = (uint8_t)((w < 2) ? 2 : ((w > 7) ? 7 : w));
      drawStarAt(cx, cy, scale, col);
      out = {(int16_t)(cx - scale * 4), (int16_t)(cy - scale * 4),
             (int16_t)(scale * 8), (int16_t)(scale * 8), true};
      break;
    }
    default: {  // STYLE_RECT — 眼睑自上而下
      int16_t vis = (int16_t)((int32_t)h * (240 - lid) / 240);
      if (vis < 5) vis = 5;
      const int16_t top = cy - h / 2 + (h - vis);
      tft.fillRect(cx - w / 2, top, w, vis, col);
      out = {(int16_t)(cx - w / 2), top, w, vis, true};
      break;
    }
  }
}

// 擦除旧矩形中未被新矩形覆盖的部分(最多 4 条边条),黑色主体直接覆盖,避免闪烁
void eraseRectOutside(const EyeRect &p, int16_t nx, int16_t ny, int16_t nw, int16_t nh) {
  if (!p.valid) return;
  const int16_t px2 = p.x + p.w, py2 = p.y + p.h;
  const int16_t nx2 = nx + nw, ny2 = ny + nh;
  if (nx >= px2 || nx2 <= p.x || ny >= py2 || ny2 <= p.y) {
    tft.fillRect(p.x, p.y, p.w, p.h, animBgColor);   // 无重叠:整块擦
    return;
  }
  if (ny > p.y)  tft.fillRect(p.x, p.y, p.w, ny - p.y, animBgColor);
  if (py2 > ny2) tft.fillRect(p.x, ny2, p.w, py2 - ny2, animBgColor);
  const int16_t iy = (p.y > ny) ? p.y : ny;
  const int16_t ih = ((py2 < ny2) ? py2 : ny2) - iy;
  if (ih > 0) {
    if (nx > p.x)  tft.fillRect(p.x, iy, nx - p.x, ih, animBgColor);
    if (px2 > nx2) tft.fillRect(nx2, iy, px2 - nx2, ih, animBgColor);
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

  static int16_t lastOx = -32768, lastOy = 0, lastW = 0, lastH = 0, lastLid = 0;
  static uint8_t lastStyle = 255;
  static bool    lastWink  = false;

  const bool winkNow = (monitorState == MON_IDLE && currentIdleExpr == IDLE_WINK && idleRightWink);
  rigZoneCleared = false;
  const bool unchanged = !rig.zoneDirty && rig.prevValid &&
      ox == lastOx && oy == lastOy && w == lastW && h == lastH &&
      lid == lastLid && (uint8_t)rig.drawnStyle == lastStyle && winkNow == lastWink;
  if (unchanged) return;          // 无任何变化:整帧跳过,不碰屏幕

  if (rig.zoneDirty) {
    tft.fillRect(0, EXPR_ZONE_Y, DISP_W, EXPR_ZONE_H, animBgColor);
    rig.prevValid = false;
    rig.zoneDirty = false;
    rigZoneCleared = true;
  }

  const int16_t cy = eyeCY() + oy;

  if (rig.drawnStyle == STYLE_RECT) {
    // 矩形眼:增量重绘——只擦旧矩形露出的边条,黑色主体直接覆盖。左右眼分别计算(WINK 右眼闭)
    int16_t vis = (int16_t)((int32_t)h * (240 - lid) / 240);
    if (vis < 5) vis = 5;
    int16_t top = cy - h / 2 + (h - vis);
    int16_t visL = vis, topL = top, visR = vis, topR = top;
    const bool winkClosed = (monitorState == MON_IDLE && currentIdleExpr == IDLE_WINK && idleRightWink);
    if (winkClosed) { visR = 10; topR = cy - 5; }   // 右眼眯成横条
    const int16_t lx = rigLCX(ox) - w / 2;
    const int16_t rx = rigRCX(ox) - w / 2;
    if (rig.prevValid) {
      eraseRectOutside(rig.prevL, lx, topL, w, visL);
      eraseRectOutside(rig.prevR, rx, topR, w, visR);
    }
    tft.fillRect(lx, topL, w, visL, C_BLACK);
    tft.fillRect(rx, topR, w, visR, C_BLACK);
    rig.prevL = {lx, topL, w, visL, true};
    rig.prevR = {rx, topR, w, visR, true};
  } else if (rig.drawnStyle == STYLE_HEART || rig.drawnStyle == STYLE_STAR) {
    // 密集位图眼(爱心/星星):原地脉动时前后图案重叠多,"擦旧+画新"在重叠像素会闪。
    // 改为:擦掉旧包围盒露出的部分(实心,不闪),再单次整块写入新位图(每像素只写一次,零闪)。
    const uint8_t  n   = (rig.drawnStyle == STYLE_HEART) ? 5 : 7;
    const uint8_t* bmp = (rig.drawnStyle == STYLE_HEART) ? HEART5 : STAR7;
    const uint8_t  scale = (rig.drawnStyle == STYLE_HEART)
                             ? (uint8_t)((w < 3) ? 3 : ((w > 9) ? 9 : w))
                             : (uint8_t)((w < 2) ? 2 : ((w > 7) ? 7 : w));
    const int16_t W = (int16_t)n * scale;
    const int16_t lx0 = rigLCX(ox) - (n / 2) * scale, rx0 = rigRCX(ox) - (n / 2) * scale;
    const int16_t y0  = cy - (n / 2) * scale;
    if (rig.prevValid) {
      eraseRectOutside(rig.prevL, lx0, y0, W, W);
      eraseRectOutside(rig.prevR, rx0, y0, W, W);
    }
    blitBitmapEye(bmp, n, rigLCX(ox), cy, scale, C_BLACK, animBgColor, rig.prevL);
    blitBitmapEye(bmp, n, rigRCX(ox), cy, scale, C_BLACK, animBgColor, rig.prevR);
  } else {
    // 线条字形眼(chevron/弧线):仅参数变化帧走到这里。
    // 用背景色按"上一帧同字形"精确重画来擦除(只抹旧笔画),而非整框刷背景——
    // 整框刷会让整块包围盒先变背景色再画黑,造成可见闪烁。
    EyeRect dump;
    if (rig.prevValid && lastStyle == (uint8_t)rig.drawnStyle && lastOx != -32768) {
      const int16_t pcy = eyeCY() + lastOy;
      drawRigEye(rigLCX(lastOx), pcy, lastW, lastH, lastLid, true,  animBgColor, dump);
      drawRigEye(rigRCX(lastOx), pcy, lastW, lastH, lastLid, false, animBgColor, dump);
    } else if (rig.prevValid) {            // 样式不匹配的兜底:整框擦一次
      tft.fillRect(rig.prevL.x - 2, rig.prevL.y - 2, rig.prevL.w + 4, rig.prevL.h + 4, animBgColor);
      tft.fillRect(rig.prevR.x - 2, rig.prevR.y - 2, rig.prevR.w + 4, rig.prevR.h + 4, animBgColor);
    }
    drawRigEye(rigLCX(ox), cy, w, h, lid, true,  C_BLACK, rig.prevL);
    drawRigEye(rigRCX(ox), cy, w, h, lid, false, C_BLACK, rig.prevR);
  }
  rig.prevValid = true;

  lastOx = ox; lastOy = oy; lastW = w; lastH = h; lastLid = lid;
  lastStyle = (uint8_t)rig.drawnStyle;
  lastWink = winkNow;
}

// ── 表情选择:状态 → 姿态 + 行为标志 ──────────────────────────
void rigApplyExpression(bool snap) {
  EyePose p = POSE_NORMAL;
  uint8_t f = RIG_BLINK | RIG_SACCADE | RIG_BREATH;

  if (monitorState == MON_IDLE) {
    switch (currentIdleExpr) {
      case IDLE_SLEEPY:    p = POSE_SLEEPY;    f = RIG_BREATH2; break;
      case IDLE_HEART:     p = POSE_HEART;     f = 0;           break;
      case IDLE_HAPPY:     p = POSE_HAPPY;     f = RIG_BREATH;  break;
      case IDLE_CURIOUS:   p = POSE_CURIOUS;   f = RIG_BLINK;   break;
      case IDLE_WINK:      p = POSE_WINK;      f = RIG_BREATH;  break;
      case IDLE_SPARKLE:   p = POSE_SPARKLE;   f = RIG_BREATH;  break;
      case IDLE_SURPRISED: p = POSE_SURPRISED; f = RIG_BLINK;   break;
      case IDLE_SHY:       p = POSE_SHY;       f = RIG_BLINK;   break;
      case IDLE_DIZZY:     p = POSE_DIZZY;     f = 0;           break;
      case IDLE_MUSIC:     p = POSE_MUSIC;     f = RIG_BREATH;  break;
      case IDLE_YAWN:      p = POSE_YAWN;      f = RIG_BREATH2; break;
      case IDLE_LOVE:      p = POSE_LOVE;      f = 0;           break;
      case IDLE_GIGGLE:    p = POSE_GIGGLE;    f = 0;           break;
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
  } else if (monitorState == MON_ALERT) {
    p = POSE_SURPRISED; f = RIG_BLINK | RIG_BREATH;   // 警觉表情,配上方 "!" 角标(alertBadgeOverlay)
  }

  // 表情身份去重:同一表情的重复推送不打断行为脚本
  uint8_t exprId = monitorState;
  if (monitorState == MON_IDLE)    exprId |= (uint8_t)(currentIdleExpr << 4);
  if (monitorState == MON_WORKING) exprId |= (uint8_t)(workAct << 4);
  static uint8_t lastExprId = 255;
  if (!snap && exprId == lastExprId) return;
  lastExprId = exprId;

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
      case IDLE_CURIOUS: {                // 东张西望 + 偶尔上抬
        static const int8_t darts[6] = {-22, 22, -22, 0, 22, 0};
        if (now >= rigBehNextMs) {
          rig.pose.ox = darts[rigBehStep % 6];
          rig.pose.oy = (rigBehStep % 3 == 0) ? -7 : 0;
          rigBehStep++;
          rigBehNextMs = now + ((rigBehStep & 1) ? 520 : 300);
        }
        break;
      }
      case IDLE_WINK:                     // 右眼周期性眨(drawRig 据此画横条)
        idleRightWink = (now % 2400) < 1100;
        break;
      case IDLE_SPARKLE:                  // 星星眼缩放闪烁
        if (now >= rigBehNextMs) { rig.pose.w = (rig.pose.w <= 6) ? 8 : 6; rigBehNextMs = now + 360; }
        break;
      case IDLE_SURPRISED: {              // 猛地瞪大(弹簧回弹),再缩回
        const bool pop = (now % 2600) < 900;
        rig.pose.w  = pop ? 42 : EYE_W;
        rig.pose.h  = pop ? 64 : EYE_H;
        rig.pose.oy = pop ? -2 : 0;
        break;
      }
      case IDLE_SHY:                      // 不时撇视
        if (now >= rigBehNextMs) { rig.pose.ox = (rigBehStep & 1) ? 9 : -9; rigBehStep++; rigBehNextMs = now + 900 + random(600); }
        break;
      case IDLE_DIZZY: {                  // 双眼画圈
        const double a = now / 360.0;
        rig.pose.ox = (int16_t)(cos(a) * 10);
        rig.pose.oy = (int16_t)(sin(a) * 6);
        break;
      }
      case IDLE_MUSIC: {                  // 随拍摇摆
        static const int8_t sway2[8] = {-7, -4, 0, 4, 7, 4, 0, -4};
        if (now >= rigBehNextMs) { rig.pose.ox = sway2[rigBehStep & 7]; rigBehStep++; rigBehNextMs = now + 220; }
        break;
      }
      case IDLE_YAWN: {                   // 眯眼打哈欠(配合 O 嘴覆盖层)
        const unsigned long c = now % 3400;
        if (c < 1700) {
          const double m = sin((double)c / 1700.0 * PI);
          rig.pose.lid = (int16_t)(40 + 200 * m);
          rig.pose.h   = (int16_t)(56 - 18 * m);
        } else { rig.pose.lid = 0; rig.pose.h = 56; }
        break;
      }
      case IDLE_LOVE:                     // 爱心跳动
        if (now >= rigBehNextMs) { rig.pose.w = (rig.pose.w == 6) ? 7 : 6; rig.pose.h = rig.pose.w; rigBehNextMs = now + 500; }
        break;
      case IDLE_GIGGLE: {                 // 憋笑上下抖
        const unsigned long c = now % 2200;
        rig.pose.oy = (c < 1100) ? (8 + (((now / 90) & 1) ? -3 : 3)) : 8;
        break;
      }
      default: break;                     // 普通/困倦:噪声层足够
    }
    return;
  }

  if (monitorState == MON_THINKING) {     // 平眼保持平静(动效交给上方省略号 thinkingDotsOverlay)
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
      case ACT_EDIT:                      // 打字机:眼随写入点移动,行尾回车归位
        if (now >= rigBehNextMs) {
          rigBehStep++;
          if (rigBehStep > EDIT_COLS) rigBehStep = 0;
          rig.pose.ox = -13 + ((int16_t)rigBehStep * 26) / EDIT_COLS;
          rigBehNextMs = now + 170;
        }
        break;
      default: {                          // ACT_WORK / ACT_AGENT:经典扫视
        static const int8_t scan[10] = {-28, -18, -8, 2, 12, 22, 28, 16, 2, -14};
        if (now >= rigBehNextMs) { rig.pose.ox = scan[rigBehStep % 10]; rigBehStep++; rigBehNextMs = now + 180; }
        break;
      }
    }
  }
}

// ── 新空闲表情覆盖层(飘字/精灵):签名门控 + 记录上一帧矩形按需擦除 ──
#define OV_MAX 6
int16_t  ovRx[OV_MAX], ovRy[OV_MAX], ovRw[OV_MAX], ovRh[OV_MAX];
uint8_t  ovRn   = 0;            // 上一帧绘制的精灵矩形数
uint32_t ovSig  = 0xFFFFFFFF;   // 上一帧签名(不变则跳过,避免静态闪烁)
uint8_t  ovExpr = 255;          // 上一帧的 idle 表情

void ovEraseAll() {
  for (uint8_t i = 0; i < ovRn; i++) tft.fillRect(ovRx[i], ovRy[i], ovRw[i], ovRh[i], animBgColor);
  ovRn = 0;
}
void ovMark(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (ovRn >= OV_MAX) return;
  ovRx[ovRn] = x; ovRy[ovRn] = y; ovRw[ovRn] = w; ovRh[ovRn] = h; ovRn++;
}
void ovText(const char* s, int16_t cx, int16_t topY, uint8_t size, uint16_t col) {
  const int16_t w = (int16_t)strlen(s) * 6 * size, h = 8 * size, x = cx - w / 2;
  tft.setTextSize(size); tft.setTextColor(col); tft.setCursor(x, topY); tft.print(s);
  tft.setTextSize(1);
  ovMark(x - 2, topY - 2, w + 4, h + 4);
}
void ovNote(int16_t x, int16_t y, uint16_t col) {   // 简易八分音符:符头 + 符干
  tft.fillRect(x, y, 6, 4, col);
  tft.fillRect(x + 5, y - 12, 2, 14, col);
  ovMark(x - 1, y - 13, 10, 20);
}

// 仅服务 idle 新表情(>=IDLE_CURIOUS)。眼睛由 drawRig 先画,这里画其上的飘字/精灵,位置一律避开眼睛矩形。
void idleNewOverlay(unsigned long now) {
  const bool active = (currentView == VIEW_MONITOR && monitorState == MON_IDLE
                       && currentIdleExpr >= IDLE_CURIOUS && rig.trans == 0);
  if (!active) {
    if (ovExpr != 255) { ovEraseAll(); ovExpr = 255; ovSig = 0xFFFFFFFF; }
    return;
  }
  if (currentIdleExpr != ovExpr) { ovEraseAll(); ovExpr = currentIdleExpr; ovSig = 0xFFFFFFFF; }

  switch (currentIdleExpr) {
    case IDLE_CURIOUS: {                    // 一跳一跳的问号
      const bool show = (now % 2600) < 1500;
      const uint32_t sig = show ? (uint32_t)(100 + (now / 60) % 90) : 0;
      if (sig == ovSig && !rigZoneCleared) break;
      ovEraseAll();
      if (show) {
        const int16_t yo = (int16_t)(sin((double)(now % 1500) / 1500.0 * PI) * 9);
        ovText("?", DISP_W / 2, 30 - yo, 3, C_WHITE);
      }
      ovSig = sig; break;
    }
    case IDLE_WINK: {                       // 上扬小嘴(静态)
      const uint32_t sig = 1;
      if (sig == ovSig && !rigZoneCleared) break;
      ovEraseAll();
      drawHappyArc(DISP_W / 2, 150, 40, C_BLACK);
      ovMark(DISP_W / 2 - 22, 138, 44, 22);
      ovSig = sig; break;
    }
    case IDLE_SPARKLE: {                    // 上方闪光点
      const bool on = ((now / 300) % 2) == 0;
      const uint32_t sig = on ? 1 : 2;
      if (sig == ovSig && !rigZoneCleared) break;
      ovEraseAll();
      if (on) {
        static const int16_t pts[3][2] = {{96, 34}, {144, 38}, {DISP_W / 2, 22}};
        for (uint8_t i = 0; i < 3; i++) {
          tft.fillRect(pts[i][0] - 4, pts[i][1], 8, 2, C_WHITE);
          tft.fillRect(pts[i][0] - 1, pts[i][1] - 4, 2, 8, C_WHITE);
          ovMark(pts[i][0] - 5, pts[i][1] - 5, 10, 10);
        }
      }
      ovSig = sig; break;
    }
    case IDLE_SURPRISED: {                  // 感叹号
      const unsigned long c = now % 2600;
      const bool show = c < 900;
      const uint32_t sig = show ? (uint32_t)(50 + c / 120) : 0;
      if (sig == ovSig && !rigZoneCleared) break;
      ovEraseAll();
      if (show) ovText("!", DISP_W / 2, 24, 3, C_WHITE);
      ovSig = sig; break;
    }
    case IDLE_SHY: {                        // 双颊腮红(缓慢脉动)
      const uint8_t p = (now / 350) % 4;
      const int16_t extra = (p < 2) ? p : (4 - p);   // 0,1,2,1
      const uint32_t sig = 10 + extra;
      if (sig == ovSig && !rigZoneCleared) break;
      ovEraseAll();
      const int16_t bw = 26 + extra * 3, bh = 12, by = 116;
      tft.fillRoundRect(45 - bw / 2,  by, bw, bh, 5, C_BLUSH); ovMark(45 - bw / 2 - 1,  by - 1, bw + 2, bh + 2);
      tft.fillRoundRect(195 - bw / 2, by, bw, bh, 5, C_BLUSH); ovMark(195 - bw / 2 - 1, by - 1, bw + 2, bh + 2);
      ovSig = sig; break;
    }
    case IDLE_DIZZY: {                      // 头顶绕圈小星
      const uint32_t sig = now / 45;
      if (sig == ovSig && !rigZoneCleared) break;
      ovEraseAll();
      for (uint8_t i = 0; i < 3; i++) {
        const double a = now / 420.0 + i * 2.094;
        const int16_t x = DISP_W / 2 + (int16_t)(cos(a) * 22);
        const int16_t y = 26 + (int16_t)(sin(a) * 12);
        drawStarAt(x, y, 2, C_WHITE);
        ovMark(x - 8, y - 8, 16, 16);
      }
      ovSig = sig; break;
    }
    case IDLE_MUSIC: {                      // 两侧升起的音符
      const uint32_t sig = now / 45;
      if (sig == ovSig && !rigZoneCleared) break;
      ovEraseAll();
      for (uint8_t i = 0; i < 2; i++) {
        const unsigned long base = now + i * 900;
        const double t = (double)(base % 1800) / 1800.0;
        const int16_t x = (i ? 222 : 14) + (int16_t)(sin(t * 6) * 4);
        const int16_t y = 150 - (int16_t)(t * 70);
        ovNote(x, y, C_WHITE);
      }
      ovSig = sig; break;
    }
    case IDLE_YAWN: {                       // O 形嘴(单次整块写入,避免脉动闪烁)
      const unsigned long c = now % 3400;
      const bool open = c < 1700;
      const double m = open ? sin((double)c / 1700.0 * PI) : 0;
      const uint32_t sig = open ? (uint32_t)(20 + (int)(m * 18)) : 0;
      if (sig == ovSig && !rigZoneCleared) break;
      if (!open) {                          // 合嘴:整块清掉上一帧的嘴(它本就要消失)
        if (ovRn > 0) tft.fillRect(ovRx[0], ovRy[0], ovRw[0], ovRh[0], animBgColor);
        ovRn = 0;
      } else {
        const int16_t mw = 10 + (int16_t)(30 * m), mh = 8 + (int16_t)(34 * m);
        const int16_t mx = DISP_W / 2 - mw / 2, my = 150 - mh / 2;
        if (ovRn > 0) {                     // 只擦旧嘴露出的部分(实心,不闪),不整块刷
          EyeRect pm = {ovRx[0], ovRy[0], ovRw[0], ovRh[0], true};
          eraseRectOutside(pm, mx, my, mw, mh);
        }
        ovRn = 0;
        GFXcanvas16 cv(mw, mh);             // 内存拼好整块再一次性推屏(每像素只写一次)
        cv.fillScreen(animBgColor);
        cv.fillRoundRect(0, 0, mw, mh, (mw < mh ? mw : mh) / 2, C_BLACK);
        tft.drawRGBBitmap(mx, my, cv.getBuffer(), mw, mh);
        ovMark(mx, my, mw, mh);
      }
      ovSig = sig; break;
    }
    case IDLE_LOVE: {                       // 升起的小爱心(白)
      const uint32_t sig = now / 45;
      if (sig == ovSig && !rigZoneCleared) break;
      ovEraseAll();
      for (uint8_t i = 0; i < 3; i++) {
        const unsigned long base = now + i * 700;
        const double t = (double)(base % 2100) / 2100.0;
        const int16_t x = DISP_W / 2 + (i - 1) * 36 + (int16_t)(sin(t * 5) * 5);
        const int16_t y = 60 - (int16_t)(t * 48);
        drawHeartAt(x, y, 3, C_WHITE);
        ovMark(x - 10, y - 10, 20, 20);
      }
      ovSig = sig; break;
    }
    case IDLE_GIGGLE: {                     // 两侧笑纹(笑时显示)
      const bool on = (now % 2200) < 1100;
      const uint32_t sig = on ? 1 : 0;
      if (sig == ovSig && !rigZoneCleared) break;
      ovEraseAll();
      if (on) {
        for (uint8_t i = 0; i < 2; i++) {
          const int16_t y = 62 + i * 7;
          tft.drawLine(20,  y, 12,  y - 4, C_WHITE); ovMark(11,  y - 5, 11, 7);
          tft.drawLine(220, y, 228, y - 4, C_WHITE); ovMark(219, y - 5, 11, 7);
        }
      }
      ovSig = sig; break;
    }
  }
}

// ALERT 告急描边(仅第 3 档):四周白色边框随脉冲呼吸;on=false 时擦掉固定 10px 边框区。
void alertBorder(bool on, unsigned long now) {
  const int16_t MAX = 10;
  tft.fillRect(0, 0, DISP_W, MAX, animBgColor);              // 先擦固定边框区(上/下/左/右)
  tft.fillRect(0, DISP_H - MAX, DISP_W, MAX, animBgColor);
  tft.fillRect(0, 0, MAX, DISP_H, animBgColor);
  tft.fillRect(DISP_W - MAX, 0, MAX, DISP_H, animBgColor);
  if (!on) return;
  const double p = (sin((double)now / 300.0) + 1.0) / 2.0;   // 0..1,~2s 呼吸
  const int16_t th = (int16_t)(p * MAX);
  if (th <= 0) return;
  tft.fillRect(0, 0, DISP_W, th, C_WHITE);
  tft.fillRect(0, DISP_H - th, DISP_W, th, C_WHITE);
  tft.fillRect(0, 0, th, DISP_H, C_WHITE);
  tft.fillRect(DISP_W - th, 0, th, DISP_H, C_WHITE);
}

// ALERT 角标:眼睛上方一个上下跳的 "!",取代旧的整屏 logo。
// 紧急度按"无人处理时长"(now-lastStatusMs)分三档:温和(0-8s)→催促(8-20s)→告急(20s+,加描边)。
// 固定区域整块擦+重绘,仅 sig 变化或刚清过表情区时才重画,避免闪烁。
void alertBadgeOverlay(unsigned long now) {
  static uint32_t lastSig = 0xFFFFFFFF;
  static bool     shown   = false;
  const int16_t BX = DISP_W / 2 - 22, BY = 12, BW = 44, BH = 60;   // 角标包围盒(EYE 区之上,容纳放大的 "!")
  const bool active = (currentView == VIEW_MONITOR && monitorState == MON_ALERT && rig.trans == 0);
  if (!active) {
    if (shown) { tft.fillRect(BX, BY, BW, BH, animBgColor); alertBorder(false, now); shown = false; lastSig = 0xFFFFFFFF; }
    return;
  }
  const unsigned long el = now - lastStatusMs;                     // 距本次 alert 推送的时长
  const uint8_t  stage  = (el >= 20000) ? 2 : (el >= 8000 ? 1 : 0);
  const uint16_t period = (stage == 2) ? 420 : (stage == 1) ? 650 : 1400;   // 跳动周期:越急越快
  const int16_t  amp    = (stage == 2) ? 5   : (stage == 1) ? 4   : 3;      // 跳幅
  const int16_t  bw     = (stage == 2) ? 9   : (stage == 1) ? 8   : 6;      // "!" 笔宽(越急越大)
  const int16_t  bh     = (stage == 2) ? 30  : (stage == 1) ? 27  : 22;
  const int16_t  dh     = (stage == 2) ? 9   : (stage == 1) ? 8   : 6;
  const int16_t  yo     = (int16_t)(sin((double)(now % period) / period * 2 * PI) * amp);

  uint32_t sig = (uint32_t)stage * 100000 + (uint32_t)(yo + 20) * 1000;
  if (stage == 2) sig += (now / 70) % 1000;                       // 告急档边框持续脉冲:~14fps 刷新
  if (sig == lastSig && shown && !rigZoneCleared) return;
  lastSig = sig;

  alertBorder(stage == 2, now);                                   // 仅告急档描边,其余档确保擦掉
  tft.fillRect(BX, BY, BW, BH, animBgColor);                      // 擦角标区
  const int16_t topY = 18 + yo;
  tft.fillRect(DISP_W / 2 - bw / 2, topY,            bw, bh, C_WHITE);   // "!" 竖条
  tft.fillRect(DISP_W / 2 - bw / 2, topY + bh + 5,   bw, dh, C_WHITE);   // "!" 点
  shown = true;
}

// 思考省略号:眼睛上方三个黑点依次起跳 "···",取代旧的 chevron 开合。
void thinkingDotsOverlay(unsigned long now) {
  static bool shown = false;
  const int16_t BX = DISP_W / 2 - 38, BY = 48, BW = 76, BH = 26;   // 三点活动带(在 EYE 区之上)
  const bool active = (currentView == VIEW_MONITOR && monitorState == MON_THINKING && rig.trans == 0);
  if (!active) {
    if (shown) { tft.fillRect(BX, BY, BW, BH, animBgColor); shown = false; }
    return;
  }
  static uint32_t lastSig = 0xFFFFFFFF;
  const uint32_t sig = (uint32_t)(now / 50);            // ~20fps 刷新足够顺滑
  if (sig == lastSig && shown && !rigZoneCleared) return;
  lastSig = sig;
  tft.fillRect(BX, BY, BW, BH, animBgColor);            // 擦整条活动带
  const int16_t cx = DISP_W / 2, baseY = 64;
  for (uint8_t i = 0; i < 3; i++) {
    const double ph = fmod((double)now / 260.0 - i + 30.0, 3.0);   // 三点依次领先一步
    const int16_t yo = (ph < 1.0) ? (int16_t)(sin(ph * PI) * 7) : 0;
    tft.fillCircle(cx - 26 + i * 26, baseY - yo, 5, C_BLACK);
  }
  shown = true;
}

// ── 覆盖层:困倦 Z 字、edit 光标、开心眼星星 ─────────────────
void rigOverlayTick() {
  static uint8_t lastZ = 255;
  static bool caretOn = false;
  static unsigned long caretMs = 0;

  if (monitorState == MON_IDLE && currentIdleExpr == IDLE_SLEEPY && rig.trans == 0) {
    const uint8_t z = 1 + (uint8_t)((millis() / 1400) % 3);
    if (z != lastZ || rigZoneCleared) {
      // zzz 在眼睛上方,从左下向右上、逐个变大依次出现
      tft.fillRect(DISP_W / 2 - 38, 2, 92, 42, animBgColor);
      tft.setTextColor(C_WHITE);
      const int16_t bx = DISP_W / 2 - 30;
      const int16_t by = 34;
      for (uint8_t i = 0; i < z; i++) {
        tft.setTextSize(i + 1);
        tft.setCursor(bx + i * 18, by - i * 12);
        tft.print("z");
      }
      tft.setTextSize(1);
      lastZ = z;
    }
  } else if (lastZ != 255) {
    tft.fillRect(DISP_W / 2 - 38, 2, 92, 42, animBgColor);
    lastZ = 255;
  }

  static uint8_t  lastEditCol = 255;   // 255=未在书写状态
  static int16_t  lastCaretX  = -1;
  static uint16_t editLine    = 0;
  if (monitorState == MON_WORKING && workAct == ACT_EDIT) {
    const unsigned long nowE = millis();
    // 防御性钳制:行为 tick 会自行归零,此处仅防未来改动失配
    const uint8_t col = (rigBehStep > EDIT_COLS) ? EDIT_COLS : rigBehStep;
    const uint16_t dimGreen = tft.color565(18, 92, 40);
    const int16_t  caretX = EDIT_LINE_X + (int16_t)col * EDIT_CELL_W;
    const bool wrapped = (lastEditCol != 255 && col < lastEditCol);
    const bool jumped  = (lastEditCol != 255 && col > lastEditCol + 1);   // 帧延迟跳格
    const bool rebuild = rigZoneCleared || lastEditCol == 255 || wrapped || jumped;

    if (caretX != lastCaretX && lastCaretX >= 0) {        // 先擦旧光标
      tft.fillRect(lastCaretX, EDIT_LINE_Y, 8, EDIT_CARET_H, animBgColor);
    }

    if (rebuild) {                                        // 整行重建
      if (lastEditCol == 255) editLine = 0;               // 重进 edit:从第 0 行开始
      if (wrapped) editLine++;
      tft.fillRect(0, EDIT_TRACE_Y, DISP_W, EDIT_BAND_H, animBgColor);
      if (editLine > 0) {                                 // 上一行暗色痕迹
        const int16_t tw = (editLine & 1) ? 150 : 110;
        for (int16_t x = EDIT_LINE_X; x < EDIT_LINE_X + tw; x += EDIT_CELL_W) {
          tft.fillRect(x, EDIT_TRACE_Y, 8, 6, dimGreen);
        }
      }
      for (uint8_t i = 0; i < col; i++) {                 // 已写格子
        if ((i * 7 + editLine * 3) % 5 == 0) continue;    // 固定花纹空格,似代码缩进
        tft.fillRect(EDIT_LINE_X + i * EDIT_CELL_W, EDIT_LINE_Y, 8, EDIT_LINE_H, C_GREEN);
      }
    } else if (col == lastEditCol + 1) {                  // 增量:补画刚写完的格子
      const uint8_t i = col - 1;
      if ((i * 7 + editLine * 3) % 5 != 0) {
        tft.fillRect(EDIT_LINE_X + i * EDIT_CELL_W, EDIT_LINE_Y, 8, EDIT_LINE_H, C_GREEN);
      }
    }
    lastEditCol = col;

    if (caretX != lastCaretX) {                           // 光标真移位:点亮并重置计时
      lastCaretX = caretX;
      caretOn = true; caretMs = nowE;
      tft.fillRect(caretX, EDIT_LINE_Y, 8, EDIT_CARET_H, C_GREEN);
    } else if (rebuild) {                                 // 区域重建但光标未动:按当前状态补画,不动计时
      if (caretOn) tft.fillRect(caretX, EDIT_LINE_Y, 8, EDIT_CARET_H, C_GREEN);
    } else if (nowE - caretMs >= 400) {                   // 原地闪烁
      caretOn = !caretOn; caretMs = nowE;
      tft.fillRect(caretX, EDIT_LINE_Y, 8, EDIT_CARET_H, caretOn ? C_GREEN : animBgColor);
    }
  } else if (lastEditCol != 255) {                        // 离开 edit:清书写区
    tft.fillRect(0, EDIT_TRACE_Y, DISP_W, EDIT_BAND_H, animBgColor);
    lastEditCol = 255; lastCaretX = -1; editLine = 0;
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

  idleNewOverlay(millis());     // 新 idle 表情(>=IDLE_CURIOUS)的飘字/精灵层
  alertBadgeOverlay(millis());  // ALERT 的 "!" 角标(取代旧整屏 logo)
  thinkingDotsOverlay(millis()); // thinking 的省略号 "···"(取代旧 chevron 开合)
}

// ── Activity ticker(仅 MON_WORKING) ─────────────────────────
void clearTicker() {
  if (!tickerVisible) return;
  tft.fillRect(0, TICKER_Y, DISP_W, TICKER_H, animBgColor);
  tickerVisible = false;
  tickerDrawn[0] = 0;
  tickerScroll = 0;
}

void drawTickerFrame(const char* txt) {
  tft.fillRect(0, TICKER_Y, DISP_W, TICKER_H, C_DARKBG);
  tft.drawFastHLine(0, TICKER_Y, DISP_W, C_MUTED);
  tft.setTextColor(C_GREEN);
  tft.setTextSize(2);
  tft.setCursor(4, TICKER_Y + 5);
  char buf[TICKER_COLS + 1];
  strncpy(buf, txt, TICKER_COLS);
  buf[TICKER_COLS] = 0;
  tft.print(buf);
  tickerVisible = true;
}

void tickTicker(unsigned long now) {
  if (monitorState != MON_WORKING || tickerText[0] == 0) {
    clearTicker();
    return;
  }
  const size_t len = strlen(tickerText);
  if (len <= TICKER_COLS) {                       // 静态:仅变化时重绘
    if (strcmp(tickerText, tickerDrawn) != 0) {
      drawTickerFrame(tickerText);
      strcpy(tickerDrawn, tickerText);
    }
    return;
  }
  if (now - tickerScrollMs < 150) return;         // 跑马灯
  tickerScrollMs = now;
  const size_t vlen = len + 3;                    // 3 格空隙
  char win[TICKER_COLS + 1];
  for (uint8_t i = 0; i < TICKER_COLS; i++) {
    const size_t idx = (tickerScroll + i) % vlen;
    win[i] = (idx < len) ? tickerText[idx] : ' ';
  }
  win[TICKER_COLS] = 0;
  drawTickerFrame(win);
  tickerScroll = (uint8_t)((tickerScroll + 1) % vlen);
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

// 开机动画:缓缓睁眼 + 俏皮 wink 打招呼(纯图形,无文字)。
// 阻塞式,仅在 setup() 开机时调用;复用现有绘图常量与 drawHappyArc/drawStarAt。
void animBootGreeting() {
  busy = true;
  const int16_t lx = eyeLX(0), rx = eyeRX(0), ey = eyeY();  // 与其它动画一致的眼睛布局
  const int16_t ecy = eyeCY();                              // 眼中心 y
  const int16_t lcx = lx + EYE_W / 2;           // 左眼中心 x = 45
  const int16_t rcx = rx + EYE_W / 2;           // 右眼中心 x = 195
  const int16_t bw = 26, bh = 12, by = 116;     // 腮红块(同 IDLE_SHY)

  tft.fillScreen(animBgColor);

  // 1) 闭眼:居中黑细线 ~0.2s
  tft.fillRect(lx, ecy - 3, EYE_W, 6, C_BLACK);
  tft.fillRect(rx, ecy - 3, EYE_W, 6, C_BLACK);
  server.handleClient();
  delay(speedMs(200));

  // 2) 缓缓睁开:可见高度 6 -> 60,居中展开,easeOutQuad ~1.0s
  const uint8_t OPEN_STEPS = 18;
  for (uint8_t s = 1; s <= OPEN_STEPS; s++) {
    float t = (float)s / OPEN_STEPS;
    float e = 1.0f - (1.0f - t) * (1.0f - t);   // ease-out
    int16_t vis = 6 + (int16_t)((EYE_H - 6) * e);
    if (vis > EYE_H) vis = EYE_H;
    const int16_t top = ecy - vis / 2;
    // 眼睛只增不减且居中,黑块逐帧"长大"覆盖上一帧,无需擦背景
    // → 避免"先刷背景再画黑"造成的脉动闪烁(见 blitBitmapEye/eraseRectOutside 注释)
    tft.fillRect(lx, top, EYE_W, vis, C_BLACK);
    tft.fillRect(rx, top, EYE_W, vis, C_BLACK);
    server.handleClient();
    delay(speedMs(55));                          // 18*55 ≈ 1.0s
  }

  // 3) 腮红浮现 ~0.3s(分级放大近似淡入)
  for (uint8_t k = 1; k <= 3; k++) {
    const int16_t w = bw * k / 3, h = bh * k / 3;
    tft.fillRoundRect(lcx - w / 2, by + (bh - h) / 2, w, h, 4, C_BLUSH);
    tft.fillRoundRect(rcx - w / 2, by + (bh - h) / 2, w, h, 4, C_BLUSH);
    server.handleClient();
    delay(speedMs(90));
  }

  // 4) 打招呼:上扬小嘴 + 右上小星 + 右眼眨一下 ~0.3s
  drawHappyArc(DISP_W / 2, 150, 40, C_BLACK);    // 上扬小嘴(同 IDLE_WINK)
  drawStarAt(206, 34, 3, C_WHITE);               // 右上小星(避开眼睛)
  server.handleClient();
  delay(speedMs(120));
  // 右眼闭:只擦细缝上下两条,中间黑缝保持不动(已是黑),避免整块橙闪
  tft.fillRect(rx, ey, EYE_W, (ecy - 3) - ey, animBgColor);
  tft.fillRect(rx, ecy + 3, EYE_W, (ey + EYE_H) - (ecy + 3), animBgColor);
  server.handleClient();
  delay(speedMs(170));
  // 右眼睁回:黑块直接长大覆盖细缝,无需先擦背景
  tft.fillRect(rx, ey, EYE_W, EYE_H, C_BLACK);
  server.handleClient();

  // 5) 定格 ~0.5s
  delay(speedMs(500));

  lastRenderKey = 255;   // 作废渲染记账,确保后续表情整屏重绘(沿用旧逻辑)
  busy = false;
}

// ── 情绪引擎 ──────────────────────────────────────────────────
// 当前心情对应的 idle 表情子集
const uint8_t* moodPool(uint8_t mood, uint8_t* sizeOut) {
  switch (mood) {
    case MOOD_FOCUSED:  *sizeOut = sizeof(MOOD_FOCUSED_POOL);  return MOOD_FOCUSED_POOL;
    case MOOD_TIRED:    *sizeOut = sizeof(MOOD_TIRED_POOL);    return MOOD_TIRED_POOL;
    case MOOD_CHEERFUL: *sizeOut = sizeof(MOOD_CHEERFUL_POOL); return MOOD_CHEERFUL_POOL;
    default:            *sizeOut = sizeof(MOOD_COZY_POOL);     return MOOD_COZY_POOL;
  }
}

// 轮播间隔随心情:雀跃快、疲惫慢,带轻微抖动更自然
uint32_t moodSwitchMs(uint8_t mood) {
  switch (mood) {
    case MOOD_CHEERFUL: return 10000 + (uint32_t)random(5000);
    case MOOD_TIRED:    return 40000 + (uint32_t)random(10000);
    case MOOD_FOCUSED:  return 25000 + (uint32_t)random(10000);
    default:            return 28000 + (uint32_t)random(10000);   // 惬意
  }
}

// 每个"其他表情"的一次动画循环约多少毫秒(用于"循环 N 次")
uint32_t otherCycleMs(uint8_t expr) {
  switch (expr) {
    case IDLE_HEART: case IDLE_LOVE:               return 900;   // 心跳脉冲
    case IDLE_SPARKLE: case IDLE_HAPPY:            return 1200;  // 闪光/星星
    case IDLE_WINK: case IDLE_GIGGLE:              return 1400;
    case IDLE_MUSIC: case IDLE_DIZZY:              return 1600;  // 摇摆/转圈
    case IDLE_YAWN: case IDLE_SLEEPY:              return 1800;  // 哈欠较慢
    default:                                       return 1300;  // curious/surprised/shy
  }
}

// 由心情挑一个"非普通"且有精神的表情。困倦/哈欠不在清醒轮播里(归入睡流程)。
uint8_t pickAwakeExpr() {
  static const uint8_t CHEER[] = { IDLE_HAPPY, IDLE_SPARKLE, IDLE_GIGGLE, IDLE_MUSIC, IDLE_HEART, IDLE_LOVE };
  static const uint8_t CALM[]  = { IDLE_CURIOUS, IDLE_WINK, IDLE_SURPRISED, IDLE_SHY, IDLE_MUSIC, IDLE_DIZZY };
  const uint8_t* pool; uint8_t n;
  if (moodJoy >= MOOD_CHEERFUL_JOY && moodEnergy > MOOD_TIRED_ENERGY) { pool = CHEER; n = sizeof(CHEER); }
  else                                                               { pool = CALM;  n = sizeof(CALM); }
  return pool[random(n)];
}

void saveMoodNow() {
  prefs.putUChar("mEnergy", (uint8_t)moodEnergy);
  prefs.putUChar("mJoy",    (uint8_t)moodJoy);
}
void loadMood() {
  moodEnergy = (float)prefs.getUChar("mEnergy", 80);
  moodJoy    = (float)prefs.getUChar("mJoy", 0);
  moodSleepiness = 0.0f;          // 开机醒着
  sleepStage     = SLEEP_AWAKE;
  sleepScriptMs  = 0;
  sleepClosed    = false;
  sleepInited    = false;
}
// 轻量持久化:最多每 5 分钟、且值有变化才写 Flash
void maybePersistMood(unsigned long now) {
  static unsigned long lastSave = 0;
  static uint8_t savedE = 255, savedJ = 255;
  if (now - lastSave < 300000UL) return;
  const uint8_t e = (uint8_t)moodEnergy, j = (uint8_t)moodJoy;
  if (e == savedE && j == savedJ) return;
  saveMoodNow();
  savedE = e; savedJ = j; lastSave = now;
}

// 每 ~2s 推进一次情绪并判定心情;心情变化且正空闲时让 idle 立刻换到新心情的表情
void updateMood(unsigned long now) {
  if (lastMoodTick == 0) { lastMoodTick = now; return; }
  if (now - lastMoodTick < 2000) return;
  const float dtMin = (now - lastMoodTick) / 60000.0f;
  lastMoodTick = now;

  const bool active = (monitorState == MON_THINKING || monitorState == MON_WORKING);
  const bool sleeping = (monitorState == MON_IDLE && sleepStage >= SLEEP_ASLEEP);
  if (active)        { moodEnergy -= ENERGY_DRAIN_PER_MIN  * dtMin; lastActiveMs = now; }
  else if (sleeping) { moodEnergy += ENERGY_SLEEP_PER_MIN  * dtMin; }   // 睡眠快速回血
  else               { moodEnergy += ENERGY_RECOVER_PER_MIN * dtMin; }
  moodEnergy = constrain(moodEnergy, 0.0f, 100.0f);
  moodJoy = constrain(moodJoy - JOY_DECAY_PER_MIN * dtMin, 0.0f, 100.0f);

  // 困意:仅 idle 累积,精力越低越快;非 idle 由 applyMonitorState 清零。
  // 累到 DROWSY_AT 即触发入睡脚本(tickSleep 接管),阶段由脚本进度决定。
  if (monitorState == MON_IDLE && !busy) {
    const float ef = SLEEPINESS_EF_HI + (SLEEPINESS_EF_LO - SLEEPINESS_EF_HI) * (1.0f - moodEnergy / 100.0f);
    moodSleepiness = constrain(moodSleepiness + SLEEPINESS_PER_MIN * ef * dtMin, 0.0f, 100.0f);
    if (sleepScriptMs == 0 && moodSleepiness >= SLEEP_DROWSY_AT) {
      sleepScriptMs = now;             // 触发入睡
      sleepStage    = SLEEP_DROWSY;
      sleepInited   = false;
      sleepClosed   = false;
      currentIdleExpr = IDLE_NORMAL;   // 让 rigBehaviorTick/覆盖层不干扰
    }
  }

  uint8_t m;
  if (moodJoy >= MOOD_CHEERFUL_JOY)               m = MOOD_CHEERFUL;
  else if (moodEnergy <= MOOD_TIRED_ENERGY)       m = MOOD_TIRED;
  else if (now - lastActiveMs < MOOD_FOCUSED_WINDOW_MS) m = MOOD_FOCUSED;
  else                                            m = MOOD_COZY;

  if (m != currentMood) {
    currentMood = m;
    // 正在 idle 显示:触发下一帧立刻换到新心情的表情子集
    if (currentView == VIEW_MONITOR && monitorState == MON_IDLE && !busy && now >= nextIdleSwitchMs) {
      lastIdleSwitch = now - nextIdleSwitchMs;
    }
  }
  maybePersistMood(now);
}

void resetIdleRotation() {
  lastIdleSwitch = millis();
  nextIdleSwitchMs = moodSwitchMs(currentMood);
  idleShowingOther = false; idleOtherLoops = 0; idlePhaseMs = millis();   // 普通为中心:回到 hub
}

// 入睡曲线数学小工具
static inline float sl_clamp(float v, float a, float b){ return v < a ? a : (v > b ? b : v); }
static inline float sl_seg(float t, float a, float b){ return sl_clamp((t - a) / (b - a), 0.f, 1.f); }
static inline float sl_eio(float t){ return t < 0.5f ? 2.f*t*t : 1.f - powf(-2.f*t+2.f, 2.f)/2.f; }
static inline float sl_eo(float t){ return 1.f - powf(1.f - t, 3.f); }
static inline float sl_lerp(float a, float b, float t){ return a + (b - a) * t; }

// 多次打哈欠的窗口起点(脚本时间 ms);穿插在下沉过程中,逐渐犯困。返回当前哈欠相位 0..1(不在窗口=0)
const uint16_t SLEEP_YAWNS[3] = {0, 4200, 8400};
static inline float sleepYawnPhase(unsigned long se){
  for (uint8_t i = 0; i < 3; i++)
    if (se >= SLEEP_YAWNS[i] && se < SLEEP_YAWNS[i] + SLP_YAWN_DUR)
      return sinf((float)(se - SLEEP_YAWNS[i]) / (float)SLP_YAWN_DUR * PI);
  return 0.f;
}

// 入睡脚本:idle 累出睡意后,从清醒平滑滑入熟睡(自然曲线,逐帧驱动 rig.pose)。
// se=脚本已进行毫秒;为对齐 mockup 时间轴用 mt=se+3000。曲线见 falling-asleep.html。
void tickSleep(unsigned long now) {
  if (currentView != VIEW_MONITOR || monitorState != MON_IDLE || busy) return;
  if (sleepScriptMs == 0) return;            // 清醒:交给轮播,不干预
  const unsigned long se = now - sleepScriptMs;

  if (se < SLP_CLOSE_AT) {                    // 入睡曲线:RECT 眼,多次哈欠 + 渐沉 + 慢眨 + 点头
    if (!sleepInited) { rigSnapPose(POSE_NORMAL, 0); sleepInited = true; }  // 从睁眼起,清屏防残留
    const float t = (float)se;
    float open;                                                    // 基线下沉(贯穿多次哈欠)
    if      (t < 10000) open = sl_lerp(1.00f, 0.42f, sl_eio(sl_seg(t, 0, 10000)));
    else if (t < 13000) open = 0.42f;
    else if (t < 15000) open = sl_lerp(0.42f, 0.12f, sl_eio(sl_seg(t, 13000, 15000)));
    else                open = sl_lerp(0.12f, 0.00f, sl_eio(sl_seg(t, 15000, 16500)));
    float oy = sinf(t / 2600.0f * 2.0f * PI) * 1.6f;              // 呼吸
    const float ym = sleepYawnPhase(se);                          // 多次打哈欠:眯眼 + 头微仰
    if (ym > 0) { open = fminf(open, 1.0f - 0.72f * ym); oy += -3.0f * ym; }
    const int16_t bw[3][2] = {{3000,500},{7000,560},{14200,760}}; // 慢眨:越往后闭得越久
    for (uint8_t i = 0; i < 3; i++)
      if (t >= bw[i][0] && t < bw[i][0] + bw[i][1])
        open *= 1.0f - sinf(sl_seg(t, bw[i][0], bw[i][0] + bw[i][1]) * PI);
    if (t >= 10000 && t < 13000) {                                // 打盹点头:沉→猛抬惊醒→再沉
      if (t < 11300)      { oy += sl_lerp(0, 16, sl_eio(sl_seg(t,10200,11300))); open = fminf(open, sl_lerp(0.40f,0.22f, sl_seg(t,10200,11300))); }
      else if (t < 11700) { float p = sl_seg(t,11300,11700); oy += sl_lerp(16,-2, sl_eo(p)); open = fmaxf(open, sl_lerp(0.22f,0.78f, sl_eo(p))); }
      else                { float p = sl_seg(t,11700,13000); oy += sl_lerp(-2,10, p);        open = fminf(open, sl_lerp(0.78f,0.40f, sl_eo(p))); }
    }
    const int16_t lid = (int16_t)(240.0f * (1.0f - sl_clamp(open, 0.f, 1.f)));
    const int16_t oyi = (int16_t)oy;
    rig.pose.style = STYLE_RECT; rig.pose.w = EYE_W; rig.pose.h = EYE_H; rig.pose.ox = 0;
    rig.pose.lid = lid; rig.pose.oy = oyi; rig.flags = 0; rig.drawnStyle = STYLE_RECT;
    springSnap(rig.ox, 0);                       springSnap(rig.oy, (int32_t)oyi << 8);
    springSnap(rig.w, (int32_t)EYE_W << 8);      springSnap(rig.h, (int32_t)EYE_H << 8);
    springSnap(rig.lid, (int32_t)lid << 8);
    sleepClosed = false;
  } else {                                    // 已合眼:睡着→熟睡(闭眼弧 + 呼吸)
    if (!sleepClosed) { sleepClosed = true; sleepStage = SLEEP_ASLEEP; rigSnapPose(POSE_DOZE, RIG_BREATH2); }
    if (se >= SLP_DEEP_AT && sleepStage != SLEEP_DEEP) { sleepStage = SLEEP_DEEP; rigSetPose(POSE_DEEPSLEEP, RIG_BREATH2); }
  }
}

// 鼻涕泡:白圈 + 高光(熟睡)
void drawSleepBubble(int16_t cx, int16_t cy, int16_t r) {
  tft.drawCircle(cx, cy, r, C_WHITE);
  tft.fillCircle(cx - r / 3, cy - r / 3, 2, C_WHITE);
}

// 入睡打哈欠的 O 形嘴(黑色圆角矩形随 sin 张合;复用 IDLE_YAWN 画法,在 drawRig 之后画)
void tickYawnMouth(unsigned long now) {
  static bool    shown = false;
  static int16_t pmw   = -1;
  const int16_t EX = 98, EY = 126, EW = 44, EH = 48;            // 擦除区(容纳最大嘴)
  const float m = (currentView == VIEW_MONITOR && monitorState == MON_IDLE && !busy
                   && sleepScriptMs != 0 && rig.trans == 0)
                  ? sleepYawnPhase(now - sleepScriptMs) : 0.f;   // 多次哈欠窗口
  if (m <= 0.02f) {
    if (shown) { tft.fillRect(EX, EY, EW, EH, animBgColor); shown = false; pmw = -1; }
    return;
  }
  const int16_t mw = 10 + (int16_t)(30 * m), mh = 8 + (int16_t)(34 * m);
  if (mw == pmw && !rigZoneCleared) return;
  pmw = mw;
  tft.fillRect(EX, EY, EW, EH, animBgColor);                    // 擦上一帧整块,防残留
  GFXcanvas16 cv(mw, mh);
  cv.fillScreen(animBgColor);
  cv.fillRoundRect(0, 0, mw, mh, (mw < mh ? mw : mh) / 2, C_BLACK);
  tft.drawRGBBitmap(120 - mw / 2, 150 - mh / 2, cv.getBuffer(), mw, mh);
  shown = true;
}

// 离开睡眠:按当时深度选花絮,清零困意并复位入睡脚本
void triggerWake(unsigned long now) {
  if (sleepScriptMs != 0) {                                // 正在入睡/睡眠中才有花絮
    wakeFlourish   = (sleepStage == SLEEP_DEEP) ? 2 : 1;   // 熟睡=惊醒,其余=温柔
    wakeFlourishMs = now;
    rigSnapPose(POSE_SHUT, RIG_BREATH);   // 闭眼矩形起手:目标态同样式平滑睁开 + 整区清屏抹残留
  }
  moodSleepiness = 0.0f;
  sleepStage     = SLEEP_AWAKE;
  sleepScriptMs  = 0;
  sleepClosed    = false;
  sleepInited    = false;
}

// 唤醒花絮:叠加在已切好的目标态之上,~600ms 非阻塞。眼睛睁开由 rig 过渡自然完成。
void tickWakeFlourish(unsigned long now) {
  if (wakeFlourish == 0) return;
  const int16_t EXX = 90, EXY = 6, EXW = 60, EXH = 38;     // "!" 区
  const int16_t BX = 80, BY = 110, BW = 80, BH = 80;       // 泡破区(须容纳半径~36 的飞溅)
  const unsigned long t = now - wakeFlourishMs;
  if (t > 600) {
    tft.fillRect(EXX, EXY, EXW, EXH, animBgColor);
    tft.fillRect(BX, BY, BW, BH, animBgColor);
    wakeFlourish = 0;
    return;
  }
  if (wakeFlourish == 2) {                                 // 惊醒:泡破 + "!"
    tft.fillRect(BX, BY, BW, BH, animBgColor);
    if (t < 220) {
      const float pt = t / 220.0f;
      for (uint8_t i = 0; i < 6; i++) {
        const float a = i / 6.0f * 6.2832f;
        const int16_t r0 = 6 + (int16_t)(pt * 16), r1 = 12 + (int16_t)(pt * 24);
        tft.drawLine(120 + cos(a) * r0, 150 + sin(a) * r0,
                     120 + cos(a) * r1, 150 + sin(a) * r1, C_WHITE);
      }
    }
    tft.fillRect(EXX, EXY, EXW, EXH, animBgColor);         // "!"
    tft.setTextColor(C_WHITE); tft.setTextSize(4);
    tft.setCursor(113, 8); tft.print('!'); tft.setTextSize(1);
  }
  // wakeFlourish==1(温柔):不画额外元素,睁眼/伸展由 rig 切到目标态完成
}

// 睡眠覆盖层:睡着/熟睡的 Zzz;熟睡额外鼻涕泡随呼吸吹大→破。脏区按签名重画,避免闪烁。
void tickSleepOverlay(unsigned long now) {
  static int8_t   lastStage = -1;
  static uint32_t lastSig   = 0xFFFFFFFF;
  const int16_t ZX = 80, ZY = 2, ZW = 92, ZH = 42;       // Zzz 区
  const int16_t BX = 94, BY = 124, BW = 52, BH = 52;     // 鼻涕泡区(眼下方)
  const bool active = (currentView == VIEW_MONITOR && monitorState == MON_IDLE
                       && sleepStage >= SLEEP_ASLEEP && rig.trans == 0);
  if (!active) {
    if (lastStage != -1) {
      tft.fillRect(ZX, ZY, ZW, ZH, animBgColor);
      tft.fillRect(BX, BY, BW, BH, animBgColor);
      lastStage = -1; lastSig = 0xFFFFFFFF;
    }
    return;
  }
  if ((int8_t)sleepStage != lastStage || rigZoneCleared) {
    tft.fillRect(ZX, ZY, ZW, ZH, animBgColor);
    tft.fillRect(BX, BY, BW, BH, animBgColor);
    lastStage = (int8_t)sleepStage; lastSig = 0xFFFFFFFF;
  }
  const uint8_t z = 1 + (uint8_t)((now / 1500) % 3);
  int16_t br = -1;
  if (sleepStage == SLEEP_DEEP) {
    const uint16_t bp = (uint16_t)(now % 4200);
    br = (bp < 3600) ? (int16_t)(4 + 18UL * bp / 3600) : -1;   // <3.6s 吹大,之后破
  }
  const uint32_t sig = (uint32_t)z * 64 + (uint32_t)(br < 0 ? 63 : br);
  if (sig == lastSig && !rigZoneCleared) return;
  lastSig = sig;

  tft.fillRect(ZX, ZY, ZW, ZH, animBgColor);                  // Zzz:由小到大向右上
  tft.setTextColor(C_WHITE);
  for (uint8_t i = 0; i < z; i++) {
    tft.setTextSize(i + 1);
    tft.setCursor(92 + i * 18, 34 - i * 12);
    tft.print('z');
  }
  tft.setTextSize(1);

  if (sleepStage == SLEEP_DEEP) {                             // 鼻涕泡
    tft.fillRect(BX, BY, BW, BH, animBgColor);
    if (br > 0) {
      drawSleepBubble(120, 150, br);
    } else {                                                  // 破裂飞溅
      for (uint8_t i = 0; i < 6; i++) {
        const float a = i / 6.0f * 6.2832f;
        tft.drawLine(120 + cos(a) * 8, 150 + sin(a) * 8,
                     120 + cos(a) * 20, 150 + sin(a) * 20, C_WHITE);
      }
    }
  }
}

void tickMonitorAnimation() {
  if (currentView != VIEW_MONITOR) return;
  if (busy) return;
  if (monitorState == MON_OFFLINE) return;

  const unsigned long now = millis();
  if (lastAnimTick != 0 && now - lastAnimTick < RIG_TICK_MS) return;
  lastAnimTick = now;

  rigBehaviorTick(now);
  tickSleep(now);        // 入睡脚本驱动 rig.pose(须在 rigTick/drawRig 之前)
  rigTick(now);
  drawRig();
  rigOverlayTick();
  tickSleepOverlay(now);
  tickYawnMouth(now);
  tickWakeFlourish(now);
  tickTicker(now);
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
  tickerVisible = false;
  tickerDrawn[0] = 0;
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
  tft.fillScreen(animBgColor);   // 清掉庆祝时画到表情区外(顶/底条)的星点
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

  // 入睡中/睡眠中:由 tickSleep 接管眼睛,不走表情轮播
  static bool wasAsleep = false;
  if (sleepScriptMs != 0) { wasAsleep = true; return; }
  if (wasAsleep) {                          // 刚醒:回普通,重置轮播
    wasAsleep = false;
    currentIdleExpr = IDLE_NORMAL;
    idleShowingOther = false; idlePhaseMs = millis();
    rigApplyExpression(false);
  }

  // 清醒:以普通为中心。普通持续 NORMAL_HOLD_MS → 穿插一个加权其他表情(循环 OTHER_LOOP_COUNT 次) → 回普通
  const unsigned long now = millis();
  if (idlePhaseMs == 0) { idlePhaseMs = now; currentIdleExpr = IDLE_NORMAL; rigApplyExpression(true); return; }

  if (!idleShowingOther) {                          // 普通 hub
    if (now - idlePhaseMs < NORMAL_HOLD_MS) return;
    currentIdleExpr  = pickAwakeExpr();
    idleShowingOther = true;
    idleOtherLoops   = 0;
    idlePhaseMs      = now;
    rigApplyExpression(false);
  } else {                                          // 穿插的其他表情:满 N 次循环回普通
    if (now - idlePhaseMs < otherCycleMs(currentIdleExpr) * (unsigned long)OTHER_LOOP_COUNT) return;
    currentIdleExpr  = IDLE_NORMAL;
    idleShowingOther = false;
    idlePhaseMs      = now;
    rigApplyExpression(false);
  }
}

uint8_t parseAct(const String& a) {
  if (a == "read")  return ACT_READ;
  if (a == "edit")  return ACT_EDIT;
  if (a == "run")   return ACT_RUN;
  if (a == "net")   return ACT_NET;
  if (a == "agent") return ACT_AGENT;
  return ACT_WORK;
}

const char* actVerb(uint8_t act) {
  switch (act) {
    case ACT_READ:  return "read";
    case ACT_EDIT:  return "edit";
    case ACT_RUN:   return "run";
    case ACT_NET:   return "net";
    case ACT_AGENT: return "agent";
    default:        return "work";
  }
}

void setTickerText(uint8_t act, const String& info) {
  char clean[22];
  uint8_t n = 0;
  for (size_t i = 0; i < info.length() && n < 21; i++) {
    const char c = info[i];
    if (c >= 0x20 && c <= 0x7E) clean[n++] = c;   // 防御:仅可打印 ASCII
  }
  clean[n] = 0;
  if (n == 0) snprintf(tickerText, sizeof(tickerText), "> %s", actVerb(act));
  else        snprintf(tickerText, sizeof(tickerText), "> %s %s", actVerb(act), clean);
  tickerScroll = 0;
  tickerDrawn[0] = 0;
}

void applyMonitorState(const String& s, const String& act, const String& info) {
  if (busy) return;   // 阻塞动画期间丢弃状态推送,防 done 递归与状态撕裂
  bootScreenDeadline = 0;   // 首个状态推送取消开机信息屏倒计时
  if (s == "done") {
    lastStatusMs = millis();
    statusTimedOut = false;
    triggerWake(millis());
    moodJoy = constrain(moodJoy + JOY_PER_DONE, 0.0f, 100.0f);   // 完成→喜悦累积(攒够→雀跃)
    currentView = VIEW_MONITOR;
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

  if (monitorState == MON_WORKING && s == "working") {
    if (act.length() > 0) {
      workAct = parseAct(act);
      setTickerText(workAct, info);
    } else {
      workAct = ACT_WORK;        // 老 hook / 缺参:经典扫视、无 ticker,与现状一致
      tickerText[0] = 0;
    }
  }

  if (monitorState == MON_THINKING || monitorState == MON_WORKING || monitorState == MON_ALERT) {
    triggerWake(millis());
  }
  // idle / offline 不唤醒、不清零困意(idle 继续累积;offline 暂停)
  lastStatusMs = millis();
  statusTimedOut = false;

  const bool entering = (currentView != VIEW_MONITOR);
  currentView = VIEW_MONITOR;
  if (entering) {
    tft.fillScreen(animBgColor);   // 从其他画面切入:整屏清理,杜绝残留
    rigInvalidate();
    tickerVisible = false;
    tickerDrawn[0] = 0;
  }

  switch (monitorState) {
    case MON_IDLE:
      if (!backlightOn) setBacklight(true);
      if (sleepScriptMs == 0) {           // 睡眠中收到 idle:不打断,继续睡(tickSleep 接管)
        resetIdleRotation();
        rigApplyExpression(entering);
      }
      break;
    case MON_THINKING:
    case MON_WORKING:
      if (!backlightOn) setBacklight(true);
      rigApplyExpression(entering);
      break;
    case MON_ALERT:
      if (!backlightOn) setBacklight(true);
      rigApplyExpression(entering);   // 警觉眼 + 上方 "!" 角标,不再整屏 logo
      clearTicker();                  // 擦掉 working 残留的底部跑马灯(只改标志不擦屏会留像素)
      break;
    case MON_OFFLINE:
      drawNormalEyes();
      setBacklight(false);
      rigInvalidate();
      tickerVisible = false;
      tickerDrawn[0] = 0;
      sleepScriptMs = 0; sleepStage = SLEEP_AWAKE; sleepClosed = false; sleepInited = false;  // 离线:复位入睡
      saveMoodNow();   // 会话结束:持久化当前情绪(重启可恢复)
      break;
  }
}

// 信息屏作为正规视图;autoLeaveMs=0 表示常驻,直到状态推送/手动切视图
void enterBootInfoView(uint32_t autoLeaveMs) {
  currentView = VIEW_BOOTINFO;
  bootScreenDeadline = autoLeaveMs ? (millis() + autoLeaveMs) : 0;
  drawWifiScreen(autoLeaveMs);
}

void enterMonitorView() {
  bootScreenDeadline = 0;        // 取消开机信息屏倒计时,防重复触发
  currentView = VIEW_MONITOR;
  statusTimedOut = false;
  lastAnimTick = 0;
  if (monitorState == MON_IDLE) resetIdleRotation();
  tft.fillScreen(animBgColor);   // 手动切入同样整屏清理
  tickerVisible = false;
  tickerDrawn[0] = 0;
  rigApplyExpression(true);   // snap:进视图立即就位
}

void checkStatusTimeout() {
  if (currentView != VIEW_MONITOR) return;
  if (lastStatusMs == 0) return;
  if (monitorState == MON_IDLE || monitorState == MON_OFFLINE) return;
  if (monitorState == MON_ALERT) return;   // alert 不自动超时:由紧急度逐级升级,直到新事件清除
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
<!doctype html><html lang="zh"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Clawd Mochi · 配网</title>
<style>
  :root{--bg:#0b0c10;--card:#15171b;--fg:#f2efe9;--muted:#9aa6b2;--accent:#da1100}
  *{box-sizing:border-box}
  body{margin:0;background:var(--bg);color:var(--fg);font-family:-apple-system,system-ui,"PingFang SC","Microsoft YaHei",sans-serif;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}
  .card{width:100%;max-width:360px;background:var(--card);border-radius:16px;padding:26px 22px;box-shadow:0 10px 30px rgba(0,0,0,.4)}
  h1{font-size:19px;margin:0 0 4px}
  .sub{color:var(--muted);font-size:13px;margin:0 0 20px;line-height:1.5}
  label{display:block;font-size:13px;color:var(--muted);margin:14px 0 6px}
  input{width:100%;padding:11px 12px;border-radius:9px;border:1px solid #2a2d33;background:#0f1115;color:var(--fg);font-size:15px}
  button{width:100%;margin-top:20px;padding:12px;border:0;border-radius:10px;background:var(--accent);color:#fff;font-size:15px;font-weight:600;cursor:pointer}
  button:disabled{opacity:.6}
  #msg{margin-top:16px;font-size:13px;min-height:18px;line-height:1.5}
  .ok{color:#50dc82}.err{color:#ff7a6a}
</style></head><body>
  <div class="card">
    <h1>Clawd Mochi 配网</h1>
    <p class="sub">连接家庭 WiFi 后，PC 的 Claude Code / Cursor 监测才能推送到设备。</p>
    <label for="ssid">WiFi 名称 (SSID)</label>
    <input id="ssid" autocomplete="off" placeholder="2.4G WiFi 名称">
    <label for="pass">WiFi 密码</label>
    <input id="pass" type="password" autocomplete="off" placeholder="密码">
    <button id="save" onclick="save()">保存并连接</button>
    <div id="msg"></div>
  </div>
<script>
function save(){
  var ssid=document.getElementById('ssid').value.trim();
  var pass=document.getElementById('pass').value;
  var msg=document.getElementById('msg'), btn=document.getElementById('save');
  if(!ssid){msg.className='err';msg.textContent='请填写 WiFi 名称';return;}
  btn.disabled=true;msg.className='';msg.textContent='连接中…';
  fetch('/wifi/save?ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass))
    .then(function(r){return r.json()})
    .then(function(j){
      if(j.sta){msg.className='ok';msg.textContent='已连接 ✓ 设备 IP：'+j.sta_ip;}
      else{msg.className='err';msg.textContent='连接失败，请检查名称/密码后重试';}
    })
    .catch(function(){msg.className='err';msg.textContent='请求失败，请重试';})
    .finally(function(){btn.disabled=false;});
}
</script>
</body></html>
)rawhtml";

// ═════════════════════════════════════════════════════════════
//  WEB ROUTES
// ═════════════════════════════════════════════════════════════

void routeRoot() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.send_P(200, "text/html", INDEX_HTML);
}

void routeStatus() {
  if (!server.hasArg("s") || server.arg("s").isEmpty()) {
    server.send(400, "application/json", "{\"e\":1}");
    return;
  }
  const String s = server.arg("s");
  const String act  = server.hasArg("act")  ? server.arg("act")  : "";
  const String info = server.hasArg("info") ? server.arg("info") : "";
  server.send(200, "application/json", "{\"ok\":1}");
  applyMonitorState(s, act, info);
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
  startMDNS();
  if (staConnected) {
    enterBootInfoView(BOOT_CONFIRM_MS);   // 连上:显示 IP 3s 后自动进表情(修复永久卡死)
  } else {
    enterBootInfoView(0);                  // 没连上:常驻,让用户重试
  }

  String j = "{\"ok\":1,\"sta\":";
  j += staConnected ? "true" : "false";
  j += ",\"sta_ip\":\"";
  j += staIP;
  j += "\"}";
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

  // ── Boot greeting shown once at startup ────────────────────
  animBootGreeting();

  // ── Start WiFi (AP + optional STA) ─────────────────────────
  prefs.begin("clawd", false);
  loadWifiCredentials();
  loadMood();   // 恢复上次的情绪(energy/joy)

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);

  if (savedSSID.length() > 0) {
    connectSTA();
    startMDNS();
  }

  if (staConnected) {
    enterBootInfoView(BOOT_CONFIRM_MS);    // 已连上:3s 确认 IP 后进表情
  } else {
    enterBootInfoView(0);                  // 未连上(无凭据 / 旧凭据连不上):常驻,等配网
  }

  // ── Register routes ────────────────────────────────────────
  server.on("/",            HTTP_GET, routeRoot);
  server.on("/status",      HTTP_GET, routeStatus);
  server.on("/wifi/save",   HTTP_GET, routeWifiSave);
  server.onNotFound(routeNotFound);
  server.begin();
}

// ═════════════════════════════════════════════════════════════
//  LOOP
// ═════════════════════════════════════════════════════════════

void loop() {
  server.handleClient();
  if (currentView == VIEW_BOOTINFO && bootScreenDeadline && millis() >= bootScreenDeadline) {
    enterMonitorView();   // enterMonitorView 内部已置 bootScreenDeadline = 0
  }
  checkStatusTimeout();
  updateMood(millis());
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
        if (!wasConnected) startMDNS();
        if (!wasConnected && currentView == VIEW_BOOTINFO) {
          enterBootInfoView(BOOT_CONFIRM_MS);   // 后台连上:刷新成绿色 IP + 3s 确认
        }
      } else {
        staIP = "";
        if (wasConnected) {                 // 在线掉线:尝试恢复一次
          connectSTA();
          if (staConnected) {
            staIP = WiFi.localIP().toString();
            startMDNS();
          } else if (currentView != VIEW_BOOTINFO) {
            enterBootInfoView(0);           // 恢复失败 → 回信息屏,稳定 AP 等重配网
          }
        }
      }
    }
  }
}
