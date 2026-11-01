#include "ui.h"
#include "cjk.h"
#include "notes.h"

#include <Arduino_GFX_Library.h>

#define LCD_BL_PIN 1

static Arduino_DataBus *s_bus = nullptr;
static Arduino_GFX     *s_gfx = nullptr;
static int s_bl_pct = 100;

// 配色
#define C_BG      0x0000
#define C_BAR     0x18C3   // 深蓝标题条
#define C_TITLE   0xFFFF
#define C_TEXT    0xEF7D
#define C_DIM     0x8410
#define C_ACCENT  0x07E0

void ui_begin() {
  ledcSetup(0, 5000, 10);
  ledcAttachPin(LCD_BL_PIN, 0);
  ledcWrite(0, 1023);

  s_bus = new Arduino_ESP32SPI(42 /*DC*/, 45 /*CS*/, 39 /*SCK*/, 38 /*MOSI*/,
                               GFX_NOT_DEFINED /*MISO*/);
  s_gfx = new Arduino_ST7789(s_bus, GFX_NOT_DEFINED /*RST*/, 1 /*横屏*/,
                             true /*IPS*/, 240, 320);
  if (!s_gfx->begin(40000000)) {
    Serial.println("[LCD] 初始化失败");
    delete s_gfx;
    s_gfx = nullptr;
  }
}

bool ui_ready() { return s_gfx != nullptr; }

void ui_backlight(int pct) {
  if (pct < 5) pct = 5;
  if (pct > 100) pct = 100;
  s_bl_pct = pct;
  ledcWrite(0, (1023 * pct) / 100);
}

int ui_backlight_get() { return s_bl_pct; }

static void clear() {
  if (!s_gfx) return;
  s_gfx->fillScreen(C_BG);
}

// 按像素宽度折行，结果放在 out[][NOTE_LINE_BYTES]
static int wrap_lines(const Note *n, char out[][NOTE_LINE_BYTES], int max_lines, int max_px) {
  int outn = 0;
  for (int i = 0; i < n->nlines && outn < max_lines; i++) {
    const char *p = n->lines[i];
    while (*p && outn < max_lines) {
      int w = 0;
      const char *q = p;
      const char *last_break = nullptr;
      while (*q) {
        uint32_t cp = 0;
        int used = cjk_next(&q, &cp);
        if (!used) break;
        int a = cjk_advance(cp);
        if (a == 0) a = 8;
        if (*q == ' ' || *q == '|') last_break = q;
        if (w + a > max_px) { q -= used; break; }
        w += a;
      }
      if (q == p) {  // 一个字符都放不下，硬塞一个
        uint32_t cp = 0;
        q = p;
        cjk_next(&q, &cp);
      }
      if (*q && last_break && last_break > p && last_break < q) q = last_break;
      int len = q - p;
      if (len > NOTE_LINE_BYTES - 1) len = NOTE_LINE_BYTES - 1;
      memcpy(out[outn], p, len);
      out[outn][len] = 0;
      // 去掉行首空格
      int k = 0;
      while (out[outn][k] == ' ') k++;
      if (k) memmove(out[outn], out[outn] + k, strlen(out[outn] + k) + 1);
      outn++;
      p = q;
      while (*p == ' ') p++;
    }
  }
  return outn;
}

void ui_show_note(int idx) {
  if (!s_gfx) return;
  if (idx < 0) idx = notes_cursor();
  const Note *n = notes_get(idx);
  if (!n) { ui_show_empty(); return; }

  clear();
  // 标题条
  s_gfx->fillRect(0, 0, 320, 28, C_BAR);
  cjk_draw(s_gfx, 8, 6, n->title[0] ? n->title : "(无标题)", C_TITLE, 250);

  // 右上角 序号/总数
  char page[16];
  snprintf(page, sizeof(page), "%d/%d", idx + 1, notes_count());
  int pw = cjk_width(page);
  cjk_draw(s_gfx, 312 - pw, 6, page, C_ACCENT);

  // 正文
  static char wrapped[16][NOTE_LINE_BYTES];
  int lines = wrap_lines(n, wrapped, 10, 308);
  int y = 36;
  for (int i = 0; i < lines; i++) {
    cjk_draw(s_gfx, 8, y, wrapped[i], C_TEXT, 316);
    y += 20;
  }

  // 底部提示
  s_gfx->drawFastHLine(0, 236, 320, C_BAR);
  cjk_draw(s_gfx, 8, 240 - 2, "BLE: ESP32-NOTE", C_DIM, 316);
}

void ui_show_empty() {
  if (!s_gfx) return;
  clear();
  s_gfx->fillRect(0, 0, 320, 28, C_BAR);
  cjk_draw(s_gfx, 8, 6, "ESP32-NOTE 票据屏", C_TITLE, 316);
  cjk_draw(s_gfx, 40, 96, "等待手机推送", C_TEXT);
  cjk_draw(s_gfx, 40, 124, "用蓝牙发送票据信息", C_DIM);
  s_gfx->drawFastHLine(0, 236, 320, C_BAR);
  cjk_draw(s_gfx, 8, 238, "BLE: ESP32-NOTE", C_DIM, 316);
}

void ui_message(const char *l1, const char *l2, const char *l3) {
  if (!s_gfx) return;
  clear();
  int y = 70;
  if (l1) { cjk_draw(s_gfx, 16, y, l1, C_TEXT, 300); y += 26; }
  if (l2) { cjk_draw(s_gfx, 16, y, l2, C_DIM, 300); y += 26; }
  if (l3) { cjk_draw(s_gfx, 16, y, l3, C_ACCENT, 300); }
}

void ui_progress(int pct, const char *title) {
  if (!s_gfx) return;
  clear();
  cjk_draw(s_gfx, 16, 60, title ? title : "升级中", C_TEXT, 300);
  int w = 300 * (pct < 0 ? 0 : (pct > 100 ? 100 : pct)) / 100;
  s_gfx->drawRect(10, 100, 300, 22, C_DIM);
  s_gfx->fillRect(11, 101, w, 20, C_ACCENT);
  char b[16];
  snprintf(b, sizeof(b), "%d%%", pct);
  cjk_draw(s_gfx, 140, 140, b, C_TEXT);
}
