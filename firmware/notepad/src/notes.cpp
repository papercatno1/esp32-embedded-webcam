#include "notes.h"
#include <FFat.h>
#include <string.h>

static Note s_notes[NOTES_MAX];
static int  s_n = 0;
static int  s_cursor = 0;
static int  s_next_id = 1;
static bool s_mounted = false;

static const char *kPath = "/notes.txt";

static void copy_str(char *dst, size_t cap, const char *src) {
  if (!src) { dst[0] = 0; return; }
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = 0;
  // 截断在 UTF-8 边界
  size_t len = strlen(dst);
  while (len && (dst[len - 1] & 0xC0) == 0x80) dst[--len] = 0;
  while (len && ((unsigned char)dst[len - 1] & 0xC0) == 0xC0) dst[--len] = 0;
}

bool notes_begin() {
  if (!s_mounted) {
    s_mounted = FFat.begin(true, "/ffat", 10, "ffat");
    if (!s_mounted) {
      Serial.println("[FFAT] 挂载失败");
      return false;
    }
  }
  Serial.printf("[FFAT] total=%u KB used=%u KB\n",
                (unsigned)(FFat.totalBytes() / 1024),
                (unsigned)(FFat.usedBytes() / 1024));

  s_n = 0;
  s_cursor = 0;
  s_next_id = 1;

  File f = FFat.open(kPath, "r");
  if (!f) {
    Serial.println("[NOTE] 无历史条目");
    return true;
  }
  Note cur;
  memset(&cur, 0, sizeof(cur));
  bool have = false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (line.endsWith("\r")) line.remove(line.length() - 1);
    if (line.startsWith("# ")) {
      if (have && s_n < NOTES_MAX) s_notes[s_n++] = cur;
      memset(&cur, 0, sizeof(cur));
      cur.id = line.substring(2).toInt();
      if (cur.id >= s_next_id) s_next_id = cur.id + 1;
      have = true;
    } else if (line.startsWith("T")) {
      copy_str(cur.title, NOTE_TITLE_BYTES, line.c_str() + 1);
    } else if (line.startsWith("L") && cur.nlines < NOTE_LINES) {
      copy_str(cur.lines[cur.nlines], NOTE_LINE_BYTES, line.c_str() + 1);
      cur.nlines++;
    }
  }
  if (have && s_n < NOTES_MAX) s_notes[s_n++] = cur;
  f.close();
  Serial.printf("[NOTE] 载入 %d 条\n", s_n);
  return true;
}

int notes_count() { return s_n; }

Note *notes_get(int idx) {
  if (idx < 0 || idx >= s_n) return nullptr;
  return &s_notes[idx];
}

Note *notes_find(int id) {
  for (int i = 0; i < s_n; i++)
    if (s_notes[i].id == id) return &s_notes[i];
  return nullptr;
}

int notes_add(const char *title, const char *const *lines, int n) {
  if (s_n >= NOTES_MAX) {  // 挤掉最旧的一条
    memmove(&s_notes[0], &s_notes[1], sizeof(Note) * (NOTES_MAX - 1));
    s_n = NOTES_MAX - 1;
  }
  Note *it = &s_notes[s_n++];
  memset(it, 0, sizeof(Note));
  it->id = s_next_id++;
  copy_str(it->title, NOTE_TITLE_BYTES, title ? title : "");
  for (int i = 0; i < n && i < NOTE_LINES; i++)
    copy_str(it->lines[i], NOTE_LINE_BYTES, lines[i] ? lines[i] : "");
  it->nlines = (n > NOTE_LINES) ? NOTE_LINES : n;
  s_cursor = s_n - 1;
  notes_save();
  return it->id;
}

bool notes_del(int id) {
  for (int i = 0; i < s_n; i++) {
    if (s_notes[i].id != id) continue;
    memmove(&s_notes[i], &s_notes[i + 1], sizeof(Note) * (s_n - i - 1));
    s_n--;
    if (s_cursor >= s_n) s_cursor = s_n ? s_n - 1 : 0;
    notes_save();
    return true;
  }
  return false;
}

void notes_clear() {
  s_n = 0;
  s_cursor = 0;
  notes_save();
}

bool notes_save() {
  if (!s_mounted) return false;
  File f = FFat.open(kPath, "w");
  if (!f) {
    Serial.println("[NOTE] 写入失败");
    return false;
  }
  for (int i = 0; i < s_n; i++) {
    f.printf("# %d\n", s_notes[i].id);
    f.printf("T%s\n", s_notes[i].title);
    for (int j = 0; j < s_notes[i].nlines; j++) f.printf("L%s\n", s_notes[i].lines[j]);
  }
  f.close();
  Serial.printf("[NOTE] 已保存 %d 条\n", s_n);
  return true;
}

int notes_cursor() { return s_cursor; }
void notes_set_cursor(int idx) {
  if (s_n == 0) { s_cursor = 0; return; }
  if (idx < 0) idx = s_n + (idx % s_n);
  s_cursor = idx % s_n;
}
