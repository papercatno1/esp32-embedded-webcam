// 点阵字体访问与绘制（16x16 / 24x24 两套）
//
// blob 格式（小端）: magic "CJKF" | gw u16 | gh u16 | count u32 |
//   count x { cp u32, advance u8, pad[3], bitmap[row_bytes * gh] }
// bitmap 按行，每行 row_bytes = (gw+7)/8 字节，高位在左。
#include "cjk.h"
#include <string.h>

extern const uint8_t _binary_data_cjk16_bin_start[];
extern const uint8_t _binary_data_cjk16_bin_end[];
extern const uint8_t _binary_data_cjk24_bin_start[];
extern const uint8_t _binary_data_cjk24_bin_end[];

struct Font {
  const uint8_t *recs;   // 记录区起点
  uint32_t count;
  uint8_t  gw, gh;
  uint8_t  row_bytes;
  uint16_t rec;
  bool     ok;
};

static Font s_font[2] = {};

#define HEADER 12

static bool load_font(Font *f, const uint8_t *start, const uint8_t *end) {
  size_t len = (size_t)(end - start);
  if (len < HEADER || memcmp(start, "CJKF", 4) != 0) return false;
  f->gw = start[4];
  f->gh = start[6];
  f->count = (uint32_t)start[8] | ((uint32_t)start[9] << 8) |
             ((uint32_t)start[10] << 16) | ((uint32_t)start[11] << 24);
  f->row_bytes = (uint8_t)((f->gw + 7) / 8);
  f->rec = (uint16_t)(8 + f->row_bytes * f->gh);
  f->recs = start + HEADER;
  size_t need = HEADER + (size_t)f->count * f->rec;
  f->ok = (need <= len);
  return f->ok;
}

void cjk_init() {
  s_font[CJK_S].ok = load_font(&s_font[CJK_S], _binary_data_cjk16_bin_start, _binary_data_cjk16_bin_end);
  s_font[CJK_L].ok = load_font(&s_font[CJK_L], _binary_data_cjk24_bin_start, _binary_data_cjk24_bin_end);
  Serial.printf("[CJK] 16px %s %u 字 / 24px %s %u 字\n",
                s_font[CJK_S].ok ? "OK" : "无效", s_font[CJK_S].count,
                s_font[CJK_L].ok ? "OK" : "无效", s_font[CJK_L].count);
}

bool cjk_ready(CjkSize sz) { return s_font[sz].ok; }

int cjk_height(CjkSize sz) { return s_font[sz].ok ? s_font[sz].gh : (sz == CJK_L ? 24 : 16); }

int cjk_line_height(CjkSize sz) { return cjk_height(sz) + 4; }

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

static const uint8_t *find_glyph(const Font *f, uint32_t cp) {
  if (!f->ok) return nullptr;
  uint32_t lo = 0, hi = f->count;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    const uint8_t *r = f->recs + (size_t)mid * f->rec;
    uint32_t v = (uint32_t)r[0] | ((uint32_t)r[1] << 8) | ((uint32_t)r[2] << 16) | ((uint32_t)r[3] << 24);
    if (v == cp) return r;
    if (v < cp) lo = mid + 1; else hi = mid;
  }
  return nullptr;
}

int cjk_advance(CjkSize sz, uint32_t cp) {
  const uint8_t *r = find_glyph(&s_font[sz], cp);
  return r ? r[4] : 0;
}

int cjk_draw(Arduino_GFX *g, int x, int y, const char *s, uint16_t color, int maxx, CjkSize sz) {
  if (!g || !s) return 0;
  const Font *f = &s_font[sz];
  const int fallback = f->ok ? f->gw : 8;
  int cx = x;
  while (*s) {
    uint32_t cp = 0;
    if (!cjk_next(&s, &cp)) break;
    const uint8_t *r = find_glyph(f, cp);
    if (!r) { cx += fallback; continue; }
    int step = r[4] ? r[4] : f->gw;
    if (maxx > 0 && cx + step > maxx + 1) break;
    const uint8_t *bmp = r + 8;
    for (int row = 0; row < f->gh; row++) {
      const uint8_t *line = bmp + row * f->row_bytes;
      int c = 0;
      while (c < f->gw) {
        // 逐位扫，连续的黑像素合并成一条横线
        if (line[c >> 3] & (0x80 >> (c & 7))) {
          int start = c;
          while (c < f->gw && (line[c >> 3] & (0x80 >> (c & 7)))) c++;
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

int cjk_width(const char *s, CjkSize sz) {
  if (!s) return 0;
  const int fallback = s_font[sz].ok ? s_font[sz].gw : 8;
  int w = 0;
  while (*s) {
    uint32_t cp = 0;
    if (!cjk_next(&s, &cp)) break;
    int a = cjk_advance(sz, cp);
    w += a ? a : fallback;
  }
  return w;
}
