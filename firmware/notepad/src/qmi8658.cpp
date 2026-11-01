// QMI8658A 最小驱动：只开加速度计，按 ±4g / 125Hz 采样
//
// 寄存器（QMI8658A 数据手册）
//   0x00 WHO_AM_I   -> 0x05
//   0x01 REVISION_ID
//   0x02 CTRL1      bit6 ADDR_AI（寄存器自增）
//   0x03 CTRL2      [7:4] aFS  [3:0] aODR
//   0x08 CTRL7      bit0 aEN  bit1 gEN
//   0x2D STATUSINT  bit0 加速度数据就绪
//   0x35 AX_L, AX_H, AY_L, AY_H, AZ_L, AZ_H
#include "qmi8658.h"

#include <Wire.h>

#define PIN_I2C_SDA 48
#define PIN_I2C_SCL 47

#define REG_WHO_AM_I 0x00
#define REG_CTRL1    0x02
#define REG_CTRL2    0x03
#define REG_CTRL7    0x08
#define REG_AX_L     0x35

#define WHO_AM_I_VAL 0x05

// CTRL2: aFS=±4g(0b0001) aODR=125Hz(0b0011)
#define CTRL2_VAL 0x13
// ±4g 时 32768 LSB = 4g
#define LSB_PER_G 8192.0f

static uint8_t s_addr = 0;
static bool    s_ok = false;
static float   s_zero = 0.0f;   // 静止时的模长校准值

static bool wr(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(s_addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool rd(uint8_t reg, uint8_t *buf, size_t n) {
  Wire.beginTransmission(s_addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)s_addr, (int)n) != (int)n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)Wire.read();
  return true;
}

uint8_t qmi_addr() { return s_addr; }
bool qmi_ok() { return s_ok; }

static bool probe(uint8_t addr) {
  s_addr = addr;
  uint8_t id = 0;
  if (!rd(REG_WHO_AM_I, &id, 1)) return false;
  Serial.printf("[IMU] 0x%02X WHO_AM_I=0x%02X\n", addr, id);
  return id == WHO_AM_I_VAL;
}

bool qmi_begin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);

  if (!probe(0x6A) && !probe(0x6B)) {
    Serial.println("[IMU] 未找到 QMI8658");
    s_ok = false;
    return false;
  }

  wr(REG_CTRL1, 0x40);        // 寄存器地址自增
  delay(2);
  wr(REG_CTRL2, CTRL2_VAL);   // ±4g / 125Hz
  delay(2);
  wr(REG_CTRL7, 0x01);        // 只开加速度计
  delay(30);

  // 读几次求出静止基线，顺便验证数据是否合理
  float sx = 0, sy = 0, sz = 0;
  int good = 0;
  for (int i = 0; i < 10; i++) {
    float ax, ay, az;
    if (qmi_read_accel(ax, ay, az)) { sx += ax; sy += ay; sz += az; good++; }
    delay(10);
  }
  if (good < 5) {
    Serial.println("[IMU] 读不到数据，判定不可用");
    s_ok = false;
    return false;
  }
  sx /= good; sy /= good; sz /= good;
  s_zero = sqrtf(sx * sx + sy * sy + sz * sz);
  Serial.printf("[IMU] 就绪 静止基线 %.2fg (x=%.2f y=%.2f z=%.2f)\n", s_zero, sx, sy, sz);

  // 基线应接近 1g；偏太多说明量程/缩放不对，但仍可用于"敲击"判定
  if (s_zero < 0.3f || s_zero > 3.0f) {
    Serial.println("[IMU] 基线异常，敲击唤醒可能不可靠");
  }
  s_ok = true;
  return true;
}

bool qmi_read_accel(float &ax, float &ay, float &az) {
  uint8_t b[6];
  if (!rd(REG_AX_L, b, 6)) return false;
  int16_t rx = (int16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
  int16_t ry = (int16_t)((uint16_t)b[2] | ((uint16_t)b[3] << 8));
  int16_t rz = (int16_t)((uint16_t)b[4] | ((uint16_t)b[5] << 8));
  ax = rx / LSB_PER_G;
  ay = ry / LSB_PER_G;
  az = rz / LSB_PER_G;
  return true;
}
