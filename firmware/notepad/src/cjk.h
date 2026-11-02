#pragma once
// 中文点阵字体访问与绘制
//
// 两套字号（由 scripts/gen_cjk_font.py 生成、编入 flash）：
//   CJK_S = 16x16  cjk16.bin  单字 40 B   小字（状态栏、页脚、页码）
//   CJK_L = 24x24  cjk24.bin  单字 80 B   大字（标题、正文）
//
// blob 格式见 scripts/gen_cjk_font.py 头部注释。
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

enum CjkSize : uint8_t {
  CJK_S = 0,   // 16 px
  CJK_L = 1,   // 24 px
};

void cjk_init();
bool cjk_ready(CjkSize sz = CJK_L);

// 该字符的推进宽度（像素）；字库里没有返回 0
int cjk_advance(CjkSize sz, uint32_t cp);

// 字形高度（= 点阵边长）
int cjk_height(CjkSize sz);

// 建议行距（字形高度 + 4）
int cjk_line_height(CjkSize sz);

// 逐字绘制 UTF-8 文本，返回实际绘制宽度；maxx 为右边界（>0 时超出即停）
int cjk_draw(Arduino_GFX *g, int x, int y, const char *s, uint16_t color,
             int maxx = -1, CjkSize sz = CJK_L);

// 按 scale 倍放大绘制（用于欢迎页大标题）
int cjk_draw_scale(Arduino_GFX *g, int x, int y, const char *s, uint16_t color,
                   int scale = 2, CjkSize sz = CJK_L);

// 整串像素宽度（不截断）
int cjk_width(const char *s, CjkSize sz = CJK_L);

// 取下一个 UTF-8 码点，返回消耗字节数（0 表示结束/非法）
int cjk_next(const char **p, uint32_t *cp);
