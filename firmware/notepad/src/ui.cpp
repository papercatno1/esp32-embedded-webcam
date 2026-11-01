#include "ui.h"
#include "cjk.h"
#include "notes.h"

#include <Arduino_GFX_Library.h>

#define LCD_BL_PIN 1

// 布局（320x240 横屏）
#define TITLE_H    34      // 标题条高度（24px 字）
#define BODY_Y     44      // 正文起始 y
#define BODY_LINES 6       // 正文行数（行距 28）
#define BODY_LH    28
#define FOOT_LINE  212     // 页脚分隔线
#define FOOT_Y     216     // 页脚文字 y（16px 字）

static Arduino_DataBus *s_bus = nullptr;
static Arduino_GFX     *s_gfx = nullptr;
static int  s_bl_pct = 100;
static bool s_screen_on = true;

// 页脚状态（由 main 更新）
static bool     s_ble_link = false;   // BLE 是否已连
static uint32_t s_ot_sec   = 60;      // 0 = 常亮
static int      s_page = 1, s_pages = 0;

// 配色
#define C_BG      0x0000
#define C_IMG_BG  0xFFFF   // 图片底色：白纸
#define C_IMG_FG  0x0000   // 图片墨色：黑字
#define C_BAR     0x18C3   // 深蓝标题条
#define C_TITLE   0xFFFF
#define C_TEXT    0xEF7D
#define C_DIM     0x8410
#define C_ACCENT  0x07E0

static void bl_apply() {
  ledcWrite(0, s_screen_on ? (1023 * s_bl_pct) / 100 : 0);
}

// SD 卡探测会把 SCK/MOSI 的引脚复用抢走，屏幕就会花屏；
// 探完卡调这个：重配总线 + 重发 LCD 初始化序列
void ui_bus_recover() {
  if (!s_bus || !s_gfx) return;
  s_bus->begin();
  s_gfx->begin();
  s_gfx->setRotation(1);
  s_gfx->fillScreen(C_BG);
}

void ui_begin() {
  ledcSetup(0, 5000, 10);
  ledcAttachPin(LCD_BL_PIN, 0);
  s_screen_on = true;
  bl_apply();

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
  bl_apply();
}

int ui_backlight_get() { return s_bl_pct; }

void ui_screen_on(bool on) {
  s_screen_on = on;
  bl_apply();
}

bool ui_screen_is_on() { return s_screen_on; }

// ---- 页脚状态 ----
void ui_set_link(bool connected) { s_ble_link = connected; }
void ui_set_timeout_s(uint32_t sec) { s_ot_sec = sec; }
void ui_set_page(int idx, int total) { s_page = idx; s_pages = total; }

void ui_footer_refresh() {
  if (!s_gfx) return;
  s_gfx->fillRect(0, FOOT_LINE, 320, 240 - FOOT_LINE, C_BG);
  s_gfx->drawFastHLine(0, FOOT_LINE, 320, C_BAR);

  // 左：蓝牙状态（连着 = 亮绿，广播中 = 灰）
  cjk_draw(s_gfx, 6, FOOT_Y, s_ble_link ? "蓝牙 已连" : "蓝牙 广播",
           s_ble_link ? C_ACCENT : C_DIM, -1, CJK_S);

  // 中：常亮 / 多少秒熄屏
  char mode[24];
  if (s_ot_sec == 0) snprintf(mode, sizeof(mode), "常亮");
  else snprintf(mode, sizeof(mode), "%lus 熄屏", (unsigned long)s_ot_sec);
  int mw = cjk_width(mode, CJK_S);
  cjk_draw(s_gfx, (320 - mw) / 2, FOOT_Y, mode, C_DIM, -1, CJK_S);

  // 右：第几条 / 共几条
  char page[20];
  if (s_pages > 0) snprintf(page, sizeof(page), "%d/%d", s_page, s_pages);
  else snprintf(page, sizeof(page), "0/0");
  int pw = cjk_width(page, CJK_S);
  cjk_draw(s_gfx, 314 - pw, FOOT_Y, page, C_DIM, -1, CJK_S);
}

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
        int a = cjk_advance(CJK_L, cp);
        if (a == 0) a = 24;
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

// 画 1-bit 位图：位=1 是白（纸），位=0 是黑（字）。按行把连续黑像素合并成一次 HLine
static void draw_bitmap_1bpp(Arduino_GFX *g, uint16_t w, uint16_t h, const uint8_t *bits) {
  const int stride = (w + 7) / 8;
  g->startWrite();          // 整屏一次开写，否则每行都要重设窗口，慢好几倍
  for (int y = 0; y < h; y++) {
    const uint8_t *p = bits + (size_t)y * stride;
    int x0 = -1;
    for (int x = 0; x <= w; x++) {
      bool black = (x < w) && !((p[x >> 3] >> (7 - (x & 7))) & 1);
      if (black && x0 < 0) x0 = x;
      else if (!black && x0 >= 0) {
        g->drawFastHLine(x0, y, x - x0, C_IMG_FG);
        x0 = -1;
      }
    }
  }
  g->endWrite();
}

void ui_show_note(int idx) {
  if (!s_gfx) return;
  if (idx < 0) idx = notes_cursor();
  const Note *n = notes_get(idx);
  if (!n) { ui_show_empty(); return; }

  // ---- 图片条目：整屏画图，页脚叠在下方（图片高度已限制在 FOOT_LINE 以内） ----
  if (n->kind == NK_IMG && n->img) {
    s_gfx->fillScreen(C_IMG_BG);
    int ox = (320 - (int)n->iw) / 2;
    if (ox < 0) ox = 0;
    s_gfx->startWrite();
    if (ox) s_gfx->writeFillRect(0, 0, ox, n->ih, C_IMG_BG);
    s_gfx->endWrite();
    draw_bitmap_1bpp(s_gfx, n->iw, n->ih, n->img);
    s_gfx->fillRect(0, FOOT_LINE, 320, 240 - FOOT_LINE, C_BAR);
    ui_set_page(idx + 1, notes_count());
    ui_footer_refresh();
    return;
  }

  clear();
  // 标题条
  s_gfx->fillRect(0, 0, 320, TITLE_H, C_BAR);
  cjk_draw(s_gfx, 6, 5, n->title[0] ? n->title : "(无标题)", C_TITLE, 246, CJK_L);

  // 右上角 序号/总数（小字）
  char page[16];
  snprintf(page, sizeof(page), "%d/%d", idx + 1, notes_count());
  int pw = cjk_width(page, CJK_S);
  cjk_draw(s_gfx, 314 - pw, 9, page, C_ACCENT, -1, CJK_S);

  // 正文
  static char wrapped[BODY_LINES][NOTE_LINE_BYTES];
  int lines = wrap_lines(n, wrapped, BODY_LINES, 306);
  int y = BODY_Y;
  for (int i = 0; i < lines; i++) {
    cjk_draw(s_gfx, 8, y, wrapped[i], C_TEXT, 312, CJK_L);
    y += BODY_LH;
  }

  // 页脚
  ui_set_page(notes_count() ? idx + 1 : 0, notes_count());
  ui_footer_refresh();
}

void ui_show_empty() {
  if (!s_gfx) return;
  clear();
  s_gfx->fillRect(0, 0, 320, TITLE_H, C_BAR);
  cjk_draw(s_gfx, 6, 5, "ESP32-NOTE 票据屏", C_TITLE, 312, CJK_L);
  cjk_draw(s_gfx, 24, 82, "等待手机推送", C_TEXT, -1, CJK_L);
  cjk_draw(s_gfx, 24, 118, "用蓝牙发送票据信息", C_DIM, -1, CJK_L);
  ui_set_page(0, 0);
  ui_footer_refresh();
}

void ui_message(const char *l1, const char *l2, const char *l3) {
  if (!s_gfx) return;
  clear();
  int y = 70;
  if (l1) { cjk_draw(s_gfx, 16, y, l1, C_TEXT, 300, CJK_L); y += 32; }
  if (l2) { cjk_draw(s_gfx, 16, y, l2, C_DIM, 300, CJK_S); y += 24; }
  if (l3) { cjk_draw(s_gfx, 16, y, l3, C_ACCENT, 300, CJK_S); }
}

void ui_progress(int pct, const char *title) {
  if (!s_gfx) return;
  clear();
  cjk_draw(s_gfx, 16, 52, title ? title : "升级中", C_TEXT, 300, CJK_L);
  int w = 300 * (pct < 0 ? 0 : (pct > 100 ? 100 : pct)) / 100;
  s_gfx->drawRect(10, 100, 300, 24, C_DIM);
  s_gfx->fillRect(11, 101, w, 22, C_ACCENT);
  char b[16];
  snprintf(b, sizeof(b), "%d%%", pct);
  cjk_draw(s_gfx, 140, 140, b, C_TEXT, -1, CJK_L);
}
