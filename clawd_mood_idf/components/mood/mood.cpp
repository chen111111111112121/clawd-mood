#include "mood.hpp"
#include "esp_random.h"
#include "nvs.h"

// IDLE_* 数值需与 monitor.hpp 一致（pickAwakeExpr 返回它们）。此处用裸数值避免反向依赖 monitor。
// 0=NORMAL 2=HEART 3=HAPPY 4=CURIOUS 5=WINK 6=SPARKLE 7=SURPRISED 8=SHY 9=DIZZY 10=MUSIC 12=LOVE 13=GIGGLE
namespace {
constexpr float ENERGY_DRAIN_PER_MIN   = 2.5f;
constexpr float ENERGY_RECOVER_PER_MIN = 7.0f;
constexpr float ENERGY_SLEEP_PER_MIN   = 18.0f;
constexpr float JOY_DECAY_PER_MIN      = 4.0f;
constexpr float JOY_PER_DONE           = 30.0f;
constexpr float MOOD_CHEERFUL_JOY      = 50.0f;
constexpr float MOOD_TIRED_ENERGY      = 30.0f;
constexpr uint32_t MOOD_FOCUSED_WINDOW_MS = 180000UL;

float    s_energy = 80.0f;
float    s_joy    = 0.0f;
uint8_t  s_mood   = mood::COZY;
uint32_t s_lastTick   = 0;
uint32_t s_lastActive = 0;

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline int32_t rrand(int32_t n) { return n > 0 ? (int32_t)(esp_random() % (uint32_t)n) : 0; }

void persist(uint32_t now) {
    static uint32_t lastSave = 0;
    static uint8_t savedE = 255, savedJ = 255;
    if (now - lastSave < 300000UL) return;                  // 最多每 5min
    const uint8_t e = (uint8_t)s_energy, j = (uint8_t)s_joy;
    if (e == savedE && j == savedJ) return;                 // 无变化不写
    nvs_handle_t h;
    if (nvs_open("clawd", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "mEnergy", e);
        nvs_set_u8(h, "mJoy", j);
        nvs_commit(h);
        nvs_close(h);
    }
    savedE = e; savedJ = j; lastSave = now;
}
} // namespace

namespace mood {

void init() {
    nvs_handle_t h;
    uint8_t e = 80, j = 0;
    if (nvs_open("clawd", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "mEnergy", &e);   // 没有键则保持默认
        nvs_get_u8(h, "mJoy", &j);
        nvs_close(h);
    }
    s_energy = (float)e;
    s_joy    = (float)j;
    s_mood   = COZY;
}

bool update(uint32_t now, bool active, bool sleeping) {
    if (s_lastTick == 0) { s_lastTick = now; return false; }
    if (now - s_lastTick < 2000) return false;
    const float dtMin = (now - s_lastTick) / 60000.0f;
    s_lastTick = now;

    if (active)        { s_energy -= ENERGY_DRAIN_PER_MIN   * dtMin; s_lastActive = now; }
    else if (sleeping) { s_energy += ENERGY_SLEEP_PER_MIN   * dtMin; }
    else               { s_energy += ENERGY_RECOVER_PER_MIN * dtMin; }
    s_energy = clampf(s_energy, 0.0f, 100.0f);
    s_joy    = clampf(s_joy - JOY_DECAY_PER_MIN * dtMin, 0.0f, 100.0f);

    uint8_t m;
    if (s_joy >= MOOD_CHEERFUL_JOY)                       m = CHEERFUL;
    else if (s_energy <= MOOD_TIRED_ENERGY)               m = TIRED;
    else if (now - s_lastActive < MOOD_FOCUSED_WINDOW_MS) m = FOCUSED;
    else                                                  m = COZY;

    const bool changed = (m != s_mood);
    s_mood = m;
    persist(now);
    return changed;
}

Mood current() { return (Mood)s_mood; }

void onDone() { s_joy = clampf(s_joy + JOY_PER_DONE, 0.0f, 100.0f); }

uint8_t pickAwakeExpr() {
    // IDLE 数值见文件头注释。CHEER:开心/星星/偷笑/听歌/爱心/花痴；CALM:好奇/眨眼/惊讶/害羞/听歌/转圈
    static const uint8_t CHEER[] = { 3, 6, 13, 10, 2, 12 };
    static const uint8_t CALM[]  = { 4, 5, 7, 8, 10, 9 };
    const uint8_t* pool; uint8_t n;
    if (s_joy >= MOOD_CHEERFUL_JOY && s_energy > MOOD_TIRED_ENERGY) { pool = CHEER; n = sizeof(CHEER); }
    else                                                           { pool = CALM;  n = sizeof(CALM); }
    return pool[rrand(n)];
}

float energy() { return s_energy; }
float joy()    { return s_joy; }

} // namespace mood
