#pragma once
// 票据/便签条目存储：RAM 里最多 8 条，落盘到 FFat(/ffat/notes.txt)
#include <Arduino.h>

#define NOTES_MAX        8
#define NOTE_LINES       12
#define NOTE_LINE_BYTES  100   // 单行 UTF-8 字节上限（约 33 个汉字）
#define NOTE_TITLE_BYTES 64    // 标题 UTF-8 字节上限

struct Note {
  int  id;
  char title[NOTE_TITLE_BYTES];
  char lines[NOTE_LINES][NOTE_LINE_BYTES];
  int  nlines;
};

// 挂载 FFat 并载入
bool notes_begin();
int  notes_count();
Note *notes_get(int idx);
Note *notes_find(int id);

// 追加一条（超过 NOTES_MAX 时挤掉最旧的），返回 id；失败 -1
int  notes_add(const char *title, const char *const *lines, int n);
bool notes_del(int id);
void notes_clear();
bool notes_save();

// 当前显示到第几条（供 UI 使用）
int  notes_cursor();
void notes_set_cursor(int idx);
