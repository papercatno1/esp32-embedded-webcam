#pragma once
// 16x16 中文点阵字体（由 scripts/gen_cjk_font.py 生成，data/cjk16.bin 嵌入）
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

void cjk_init();
bool cjk_ready();

// 返回该字符的推进宽度（像素）；找不到返回 0
int cjk_advance(uint32_t cp);

// 逐字绘制 UTF-8 文本，返回实际绘制宽度。
// maxx 为右边界（>0 时超出即停止）
int cjk_draw(Arduino_GFX *g, int x, int y, const char *s, uint16_t color, int maxx = -1);

// 整串像素宽度（不截断）
int cjk_width(const char *s);

// 取 UTF-8 下一个码点，返回消耗字节数（0 表示结束/非法）
int cjk_next(const char **p, uint32_t *cp);
