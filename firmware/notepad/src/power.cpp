#include "power.h"
#include "ui.h"
#include "qmi8658.h"

#define TAP_THRESHOLD  0.75f   // 相邻两次采样的加速度变化量阈值（g，三轴绝对值之和）
#define TAP_COOLDOWN   500     // 两次敲击最小间隔（ms）
#define SAMPLE_MS      25      // 熄屏时采样周期（ms）

static uint32_t s_timeout_ms = 60000;
static uint32_t s_last_ms    = 0;
static bool     s_blank      = false;

static float    s_px = 0, s_py = 0, s_pz = 0;
static bool     s_have_prev  = false;
static uint32_t s_last_tap   = 0;
static float    s_last_impulse = 0;

void power_begin(uint32_t idle_ms) {
  s_timeout_ms = idle_ms;
  s_last_ms = millis();
  s_blank = false;
  ui_screen_on(true);
  Serial.printf("[PWR] 熄屏超时 %lus（0=常亮），IMU %s\n",
                (unsigned long)(s_timeout_ms / 1000), qmi_ok() ? "可用" : "不可用");
}

void power_set_timeout(uint32_t ms) {
  s_timeout_ms = ms;
  s_last_ms = millis();
  if (ms == 0 && s_blank) power_wake();
}

uint32_t power_timeout() { return s_timeout_ms; }

void power_wake() {
  if (s_blank) Serial.println("[PWR] 唤醒");
  s_blank = false;
  ui_screen_on(true);
  s_last_ms = millis();
}

void power_blank() {
  if (!s_blank) Serial.println("[PWR] 熄屏");
  s_blank = true;
  ui_screen_on(false);
  s_have_prev = false;
}

void power_activity() {
  s_last_ms = millis();
  if (s_blank) power_wake();
}

bool power_is_blank() { return s_blank; }

uint32_t power_idle_ms() { return millis() - s_last_ms; }

float power_last_impulse() { return s_last_impulse; }

void power_tick() {
  uint32_t now = millis();

  // 超时熄屏（没有 IMU 就不熄，避免唤不醒）
  if (!s_blank && s_timeout_ms > 0 && qmi_ok() && (now - s_last_ms) > s_timeout_ms) {
    power_blank();
  }

  if (!s_blank) return;

  // 熄屏期间低频采样，靠加速度突变判断敲击
  static uint32_t last_sample = 0;
  if (now - last_sample < SAMPLE_MS) return;
  last_sample = now;

  float ax, ay, az;
  if (!qmi_read_accel(ax, ay, az)) return;

  if (!s_have_prev) { s_px = ax; s_py = ay; s_pz = az; s_have_prev = true; return; }

  float d = fabsf(ax - s_px) + fabsf(ay - s_py) + fabsf(az - s_pz);
  s_px = ax; s_py = ay; s_pz = az;

  if (d > TAP_THRESHOLD && (now - s_last_tap) > TAP_COOLDOWN) {
    s_last_tap = now;
    s_last_impulse = d;
    Serial.printf("[PWR] 敲击 %.2fg -> 点亮\n", d);
    power_wake();
  }
}
