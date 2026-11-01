#pragma once
// 票据/便签条目存储：RAM 里最多 8 条（其中图片最多 3 条）
// 文本落盘 FFat(/notes.txt)，图片以 1-bit RLE 落盘 /i<id>.rle + 索引 /imgs.txt
#include <Arduino.h>

#define NOTES_MAX        8
#define NOTE_LINES       12
#define NOTE_LINE_BYTES  100   // 单行 UTF-8 字节上限（约 33 个汉字）
#define NOTE_TITLE_BYTES 64    // 标题 UTF-8 字节上限

#define NOTES_IMG_MAX    3
#define IMG_MAX_W        320
#define IMG_MAX_H        212   // 底部 28px 留给页脚

enum NoteKind : uint8_t { NK_TEXT = 0, NK_IMG = 1 };

struct Note {
  int     id;
  uint8_t kind;
  uint16_t iw, ih;                        // 图片像素尺寸（kind==NK_IMG 有效）
  char    title[NOTE_TITLE_BYTES];
  char    lines[NOTE_LINES][NOTE_LINE_BYTES];
  int     nlines;
  uint8_t *img;                           // 1-bit 位图（PSRAM，行字节对齐、MSB 在前、1=白 0=黑）
};

// 挂载 FFat 并载入（文本 + 图片）
bool notes_begin();
int  notes_count();
Note *notes_get(int idx);
Note *notes_find(int id);

// 追加一条文本（超过 NOTES_MAX 时挤掉最旧的），返回 id；失败 -1
int  notes_add(const char *title, const char *const *lines, int n);
// 追加一张图片：rle 为 [count u8][val u8] 序列（val=0x00 黑 / 0xFF 白），解码后存位图
int  notes_add_img_rle(const char *title, uint16_t w, uint16_t h,
                       const uint8_t *rle, size_t rle_len, bool persist = true);
int  notes_img_count();
uint32_t notes_img_hash(const uint8_t *bits, size_t n);   // FNV-1a，用来核对传过去的位图
void notes_refresh_img_fs();          // 探到卡之后调一次：图片改存 TF 卡
const char *notes_img_fs_name();      // "sd" / "ffat"，给 status.json 用

bool notes_del(int id);
void notes_clear();
bool notes_save();

// 当前显示到第几条（供 UI 使用）
int  notes_cursor();
void notes_set_cursor(int idx);
