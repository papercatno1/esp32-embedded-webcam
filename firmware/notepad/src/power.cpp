#include "power.h"
#include "ui.h"
#include "qmi8658.h"

// ---------------- 打击/甩动判定参数 ----------------
#define TAP_THRESHOLD  0.75f   // 相邻两次采样的加速度变化量阈值（g，三轴绝对值之和）
#define TAP_COOLDOWN   500     // 两次敲击最小间隔（ms）
#define SAMPLE_MS      25      // 采样周期（ms）

#define SWIPE_START_DPS 120.0f  // 角速度超过这个值开始记录一次甩动
#define SWIPE_END_DPS    45.0f  // 低于这个值算这次甩动结束
#define SWIPE_MIN_DEG    50.0f  // 累计转过的角度超过多少度才算数
#define SWIPE_COOLDOWN   700    // 两次甩动最小间隔（ms）
// 方向：+1 = 顺时针（多数人理解的"往右甩"）；方向反了就改成 -1
#define SWIPE_DIR_SIGN   1

static uint32_t s_timeout_ms = 60000;
static uint32_t s_last_ms    = 0;
static bool     s_blank      = false;

static float    s_px = 0, s_py = 0, s_pz = 0;
static bool     s_have_prev  = false;
static uint32_t s_last_tap   = 0;
static float    s_last_impulse = 0;

// 甩动状态
static bool     s_in_gesture = false;
static float    s_deg_x = 0, s_deg_y = 0, s_deg_z = 0;   // 累计角度（度）
static uint32_t s_last_swipe = 0;
static float    s_last_deg = 0;
static char     s_last_axis = '-';
static int      s_pending = 0;       // -1 上一条 / +1 下一条
static uint32_t s_swipe_count = 0;

void power_begin(uint32_t idle_ms) {
  s_timeout_ms = idle_ms;
  s_last_ms = millis();
  s_blank = false;
  s_pending = 0;
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
  s_in_gesture = false;
}

void power_activity() {
  s_last_ms = millis();
  if (s_blank) power_wake();
}

bool power_is_blank() { return s_blank; }

uint32_t power_idle_ms() { return millis() - s_last_ms; }

float power_last_impulse() { return s_last_impulse; }

int power_take_swipe() {
  int v = s_pending;
  s_pending = 0;
  return v;
}

float power_last_swipe_deg() { return s_last_deg; }
char  power_last_swipe_axis() { return s_last_axis; }
uint32_t power_swipe_count() { return s_swipe_count; }

// 记录一次甩动：挑累计角度最大的轴，用它的符号定方向
static void swipe_finish() {
  float mx = fabsf(s_deg_x), my = fabsf(s_deg_y), mz = fabsf(s_deg_z);
  char axis = 'z';
  float val = s_deg_z;
  if (mx >= my && mx >= mz) { axis = 'x'; val = s_deg_x; }
  else if (my >= mx && my >= mz) { axis = 'y'; val = s_deg_y; }

  s_deg_x = s_deg_y = s_deg_z = 0;

  if (fabsf(val) < SWIPE_MIN_DEG) return;
  if (millis() - s_last_swipe < SWIPE_COOLDOWN) return;

  s_last_swipe = millis();
  s_last_deg = val;
  s_last_axis = axis;
  s_swipe_count++;

  int dir = (val > 0) ? SWIPE_DIR_SIGN : -SWIPE_DIR_SIGN;
  Serial.printf("[PWR] 甩动 轴=%c 转=%.1f度 -> %s\n", axis, val,
                dir > 0 ? "下一条" : "上一条");

  if (s_blank) {
    power_wake();      // 熄屏时先亮，不翻页
  } else {
    s_pending = dir;
    s_last_ms = millis();
  }
}

void power_tick() {
  uint32_t now = millis();

  // 超时熄屏（没有 IMU 就不熄，避免唤不醒）
  if (!s_blank && s_timeout_ms > 0 && qmi_ok() && (now - s_last_ms) > s_timeout_ms) {
    power_blank();
  }

  if (!qmi_ok()) return;

  static uint32_t last_sample = 0;
  if (now - last_sample < SAMPLE_MS) return;
  last_sample = now;

  float ax, ay, az, gx, gy, gz;
  if (!qmi_read_motion(ax, ay, az, gx, gy, gz)) return;

  // ---- 敲击（只在熄屏时判，避免正常拿动就亮） ----
  if (s_blank) {
    if (!s_have_prev) {
      s_px = ax; s_py = ay; s_pz = az; s_have_prev = true;
    } else {
      float d = fabsf(ax - s_px) + fabsf(ay - s_py) + fabsf(az - s_pz);
      s_px = ax; s_py = ay; s_pz = az;
      if (d > TAP_THRESHOLD && (now - s_last_tap) > TAP_COOLDOWN) {
        s_last_tap = now;
        s_last_impulse = d;
        Serial.printf("[PWR] 敲击 %.2fg -> 点亮\n", d);
        power_wake();
      }
    }
  }

  // ---- 甩动翻页（亮屏时生效；熄屏时也能用来唤醒） ----
  float mag = sqrtf(gx * gx + gy * gy + gz * gz);
  if (!s_in_gesture) {
    if (mag > SWIPE_START_DPS) {
      s_in_gesture = true;
      s_deg_x = gx * (SAMPLE_MS / 1000.0f);
      s_deg_y = gy * (SAMPLE_MS / 1000.0f);
      s_deg_z = gz * (SAMPLE_MS / 1000.0f);
    }
  } else {
    s_deg_x += gx * (SAMPLE_MS / 1000.0f);
    s_deg_y += gy * (SAMPLE_MS / 1000.0f);
    s_deg_z += gz * (SAMPLE_MS / 1000.0f);
    if (mag < SWIPE_END_DPS) {
      s_in_gesture = false;
      swipe_finish();
    }
  }
}
