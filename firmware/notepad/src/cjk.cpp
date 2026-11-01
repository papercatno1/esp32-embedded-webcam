// 点阵字体访问与绘制
//
// blob 格式（小端）：magic "CJKF" | gw u16 | gh u16 | count u32 |
//                    count x { cp u32, advance u8, pad[3], bitmap[32] }
// bitmap 按行，每行 gw 位（2 字节，高位在左），共 gh 行。
#include "cjk.h"
#include <string.h>

extern const uint8_t _binary_data_cjk16_bin_start[];
extern const uint8_t _binary_data_cjk16_bin_end[];

static const uint8_t *s_data = nullptr;
static uint32_t s_count = 0;
static uint8_t  s_gw = 16, s_gh = 16;

#define REC 40

void cjk_init() {
  const uint8_t *d = _binary_data_cjk16_bin_start;
  size_t len = _binary_data_cjk16_bin_end - _binary_data_cjk16_bin_start;
  if (len < 12 || memcmp(d, "CJKF", 4) != 0) {
    Serial.println("[CJK] 字库无效");
    return;
  }
  s_gw    = (uint16_t)d[4] | ((uint16_t)d[5] << 8);
  s_gh    = (uint16_t)d[6] | ((uint16_t)d[7] << 8);
  s_count = (uint32_t)d[8] | ((uint32_t)d[9] << 8) | ((uint32_t)d[10] << 16) | ((uint32_t)d[11] << 24);
  s_data  = d + 12;
  Serial.printf("[CJK] 字库就绪 %ux%u, %u 字\n", s_gw, s_gh, s_count);
}

bool cjk_ready() { return s_data != nullptr; }

int cjk_next(const char **p, uint32_t *cp) {
  const uint8_t *s = (const uint8_t *)*p;
  if (!s || !*s) return 0;
  uint32_t c;
  int n;
  if (s[0] < 0x80)              { c = s[0]; n = 1; }
  else if ((s[0] & 0xE0) == 0xC0 && s[1]) { c = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F); n = 2; }
  else if ((s[0] & 0xF0) == 0xE0 && s[1] && s[2]) {
    c = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); n = 3;
  } else if ((s[0] & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) {
    c = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F); n = 4;
  } else { c = '?'; n = 1; }
  *cp = c;
  *p += n;
  return n;
}

static const uint8_t *find_glyph(uint32_t cp) {
  if (!s_data) return nullptr;
  uint32_t lo = 0, hi = s_count;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    const uint8_t *r = s_data + (size_t)mid * REC;
    uint32_t v = (uint32_t)r[0] | ((uint32_t)r[1] << 8) | ((uint32_t)r[2] << 16) | ((uint32_t)r[3] << 24);
    if (v == cp) return r;
    if (v < cp) lo = mid + 1; else hi = mid;
  }
  return nullptr;
}

int cjk_advance(uint32_t cp) {
  const uint8_t *r = find_glyph(cp);
  return r ? r[4] : 0;
}

int cjk_draw(Arduino_GFX *g, int x, int y, const char *s, uint16_t color, int maxx) {
  if (!g || !s) return 0;
  int cx = x;
  while (*s) {
    uint32_t cp = 0;
    if (!cjk_next(&s, &cp)) break;
    const uint8_t *r = find_glyph(cp);
    if (!r) { cx += 8; continue; }
    int step = r[4] ? r[4] : s_gw;
    if (maxx > 0 && cx + step > maxx + 1) break;
    for (int row = 0; row < s_gh; row++) {
      uint16_t bits = (uint16_t)r[8 + row * 2] | ((uint16_t)r[9 + row * 2] << 8);
      int c = 0;
      while (c < s_gw) {
        if (bits & (1u << (s_gw - 1 - c))) {
          int start = c;
          while (c < s_gw && (bits & (1u << (s_gw - 1 - c)))) c++;
          g->drawFastHLine(cx + start, y + row, c - start, color);
        } else {
          c++;
        }
      }
    }
    cx += step;
  }
  return cx - x;
}

int cjk_width(const char *s) {
  if (!s) return 0;
  int w = 0;
  while (*s) {
    uint32_t cp = 0;
    if (!cjk_next(&s, &cp)) break;
    int a = cjk_advance(cp);
    w += a ? a : 8;
  }
  return w;
}
