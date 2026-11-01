#include "notes.h"
#include <FFat.h>
#include <SD.h>
#include <string.h>

static Note s_notes[NOTES_MAX];
static int  s_n = 0;
static int  s_cursor = 0;
static int  s_next_id = 1;
static bool s_mounted = false;

static const char *kPath    = "/notes.txt";
static const char *kImgIdx  = "/imgs.txt";

static void copy_str(char *dst, size_t cap, const char *src) {
  if (!src) { dst[0] = 0; return; }
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = 0;
  // 截断在 UTF-8 边界
  size_t len = strlen(dst);
  while (len && (dst[len - 1] & 0xC0) == 0x80) dst[--len] = 0;
  while (len && ((unsigned char)dst[len - 1] & 0xC0) == 0xC0) dst[--len] = 0;
}

// ---------------- 1-bit 位图 / RLE ----------------
// 位图：行字节对齐（stride = (w+7)/8），每字节 MSB 是左边那个像素，1 = 白、0 = 黑
// RLE ：[count u8][value u8] 成对，count 1..255，value 只能是 0x00 或 0xFF

static inline size_t bmp_stride(uint16_t w) { return (w + 7) / 8; }
static inline size_t bmp_bytes(uint16_t w, uint16_t h) { return bmp_stride(w) * h; }

static bool rle_decode(const uint8_t *rle, size_t n, uint8_t *out, size_t out_len) {
  size_t o = 0;
  for (size_t i = 0; i + 1 < n && o < out_len; i += 2) {
    uint8_t cnt = rle[i];
    uint8_t val = rle[i + 1];
    for (uint8_t k = 0; k < cnt && o < out_len; k++) out[o++] = val;
  }
  return o == out_len;
}

static size_t rle_encode(const uint8_t *bits, size_t n, uint8_t *out, size_t cap) {
  size_t o = 0, i = 0;
  while (i < n) {
    uint8_t v = bits[i];
    size_t run = 1;
    while (i + run < n && bits[i + run] == v && run < 255) run++;
    if (o + 2 > cap) return 0;
    out[o++] = (uint8_t)run;
    out[o++] = v;
    i += run;
  }
  return o;
}


// 图片能落 SD 卡就落卡（29 GB），没有卡才用内部 FFat（10 MB）
static bool s_img_on_sd = false;

static fs::FS &img_fs() { return s_img_on_sd ? (fs::FS &)SD : (fs::FS &)FFat; }

void notes_refresh_img_fs() {
  s_img_on_sd = (SD.cardType() != CARD_NONE);
  Serial.printf("[IMG] 图片存放：%s\n", s_img_on_sd ? "TF 卡" : "内部 FFat");
}

const char *notes_img_fs_name() { return s_img_on_sd ? "sd" : "ffat"; }

static void img_path(int id, char *buf, size_t cap) {
  snprintf(buf, cap, "/i%d.rle", id);
}

int notes_img_count() {
  int c = 0;
  for (int i = 0; i < s_n; i++) if (s_notes[i].kind == NK_IMG) c++;
  return c;
}

static void drop_note_at(int i) {
  Note *it = &s_notes[i];
  if (it->img) {
    char path[24];
    img_path(it->id, path, sizeof(path));
    img_fs().remove(path);
    free(it->img);
    it->img = nullptr;
  }
  memmove(&s_notes[i], &s_notes[i + 1], sizeof(Note) * (s_n - i - 1));
  s_n--;
}

// ---------------- 落盘 ----------------
static void save_img_index() {
  if (!s_mounted) return;
  File f = img_fs().open(kImgIdx, "w");
  if (!f) return;
  for (int i = 0; i < s_n; i++) {
    Note *it = &s_notes[i];
    if (it->kind != NK_IMG || !it->img) continue;
    size_t bytes = bmp_bytes(it->iw, it->ih);
    uint8_t *rle = (uint8_t *)malloc(bytes * 2 + 64);
    if (!rle) continue;
    size_t rlen = rle_encode(it->img, bytes, rle, bytes * 2 + 64);
    if (rlen) {
      char path[24];
      img_path(it->id, path, sizeof(path));
      File g = img_fs().open(path, "w");
      if (g) {
        uint8_t hdr[4] = { (uint8_t)(it->iw & 0xFF), (uint8_t)(it->iw >> 8),
                           (uint8_t)(it->ih & 0xFF), (uint8_t)(it->ih >> 8) };
        g.write(hdr, 4);
        g.write(rle, rlen);
        g.close();
        f.printf("# %d %u %u %u\n", it->id, it->iw, it->ih, (unsigned)rlen);
      }
    }
    free(rle);
  }
  f.close();
}

bool notes_save() {
  if (!s_mounted) return false;
  File f = FFat.open(kPath, "w");
  if (!f) {
    Serial.println("[NOTE] 写入失败");
    return false;
  }
  int n_text = 0;
  for (int i = 0; i < s_n; i++) {
    if (s_notes[i].kind != NK_TEXT) continue;
    f.printf("# %d\n", s_notes[i].id);
    f.printf("T%s\n", s_notes[i].title);
    for (int j = 0; j < s_notes[i].nlines; j++) f.printf("L%s\n", s_notes[i].lines[j]);
    n_text++;
  }
  f.close();
  save_img_index();
  Serial.printf("[NOTE] 已保存 文本 %d 条 / 图片 %d 张\n", n_text, notes_img_count());
  return true;
}

// ---------------- 载入 ----------------
static void load_images() {
  File f = img_fs().open(kImgIdx, "r");
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (line.endsWith("\r")) line.remove(line.length() - 1);
    if (!line.startsWith("# ")) continue;
    int id = 0, w = 0, h = 0, rlen = 0;
    if (sscanf(line.c_str() + 2, "%d %d %d %d", &id, &w, &h, &rlen) != 4) continue;
    if (w <= 0 || w > IMG_MAX_W || h <= 0 || h > IMG_MAX_H || rlen <= 0) continue;
    if (s_n >= NOTES_MAX) break;

    char path[24];
    img_path(id, path, sizeof(path));
    File g = img_fs().open(path, "r");
    if (!g) continue;
    uint8_t hdr[4] = {0};
    if (g.read(hdr, 4) != 4) { g.close(); continue; }
    uint8_t *rle = (uint8_t *)malloc(rlen);
    size_t got = rle ? g.read(rle, rlen) : 0;
    g.close();
    if (!rle || got != (size_t)rlen) { free(rle); continue; }

    size_t bytes = bmp_bytes(w, h);
    uint8_t *bits = (uint8_t *)ps_malloc(bytes);
    if (!bits) bits = (uint8_t *)malloc(bytes);
    bool ok = bits && rle_decode(rle, got, bits, bytes);
    free(rle);
    if (!ok) { free(bits); continue; }

    Note *it = &s_notes[s_n++];
    memset(it, 0, sizeof(Note));
    it->id = id;
    it->kind = NK_IMG;
    it->iw = w;
    it->ih = h;
    it->img = bits;
    snprintf(it->title, NOTE_TITLE_BYTES, "图片 %d", id);
    if (id >= s_next_id) s_next_id = id + 1;
  }
  f.close();
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
  if (f) {
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
        cur.kind = NK_TEXT;
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
  }
  int n_text = s_n;
  load_images();
  Serial.printf("[NOTE] 载入 文本 %d 条 / 图片 %d 张\n", n_text, notes_img_count());
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
  if (s_n >= NOTES_MAX) drop_note_at(0);   // 挤掉最旧的一条
  Note *it = &s_notes[s_n++];
  memset(it, 0, sizeof(Note));
  it->id = s_next_id++;
  it->kind = NK_TEXT;
  copy_str(it->title, NOTE_TITLE_BYTES, title ? title : "");
  for (int i = 0; i < n && i < NOTE_LINES; i++)
    copy_str(it->lines[i], NOTE_LINE_BYTES, lines[i] ? lines[i] : "");
  it->nlines = (n > NOTE_LINES) ? NOTE_LINES : n;
  s_cursor = s_n - 1;
  notes_save();
  return it->id;
}

uint32_t notes_img_hash(const uint8_t *bits, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) { h ^= bits[i]; h *= 16777619u; }
  return h;
}

int notes_add_img_rle(const char *title, uint16_t w, uint16_t h,
                      const uint8_t *rle, size_t rle_len, bool persist) {
  if (w == 0 || w > IMG_MAX_W || h == 0 || h > IMG_MAX_H || !rle || rle_len < 2) return -1;

  size_t bytes = bmp_bytes(w, h);
  uint8_t *bits = (uint8_t *)malloc(bytes);   // 内部 RAM：8.5KB 一张，够用且稳定
  if (!bits) {
    Serial.println("[IMG] 位图内存不足");
    return -1;
  }
  if (!rle_decode(rle, rle_len, bits, bytes)) {
    Serial.printf("[IMG] RLE 解压后长度不符（期望 %u 字节）\n", (unsigned)bytes);
    free(bits);
    return -1;
  }

  // 图片条数上限：挤掉最旧的图片
  while (notes_img_count() >= NOTES_IMG_MAX) {
    for (int i = 0; i < s_n; i++) {
      if (s_notes[i].kind == NK_IMG) { drop_note_at(i); break; }
    }
  }
  if (s_n >= NOTES_MAX) drop_note_at(0);

  Note *it = &s_notes[s_n++];
  memset(it, 0, sizeof(Note));
  it->id = s_next_id++;
  it->kind = NK_IMG;
  it->iw = w;
  it->ih = h;
  it->img = bits;
  if (title && title[0]) copy_str(it->title, NOTE_TITLE_BYTES, title);
  else snprintf(it->title, NOTE_TITLE_BYTES, "图片 %d", it->id);
  s_cursor = s_n - 1;
  if (persist) notes_save();   // 调用方可先拿 id 回执、再落盘（写 flash 期间 BLE 会卡住）
  return it->id;
}

bool notes_del(int id) {
  for (int i = 0; i < s_n; i++) {
    if (s_notes[i].id != id) continue;
    drop_note_at(i);
    if (s_cursor >= s_n) s_cursor = s_n ? s_n - 1 : 0;
    notes_save();
    return true;
  }
  return false;
}

void notes_clear() {
  for (int i = 0; i < s_n; i++) {
    if (s_notes[i].img) {
      char path[24];
      img_path(s_notes[i].id, path, sizeof(path));
      img_fs().remove(path);
      free(s_notes[i].img);
      s_notes[i].img = nullptr;
    }
  }
  s_n = 0;
  s_cursor = 0;
  if (s_mounted) img_fs().remove(kImgIdx);
  notes_save();
}

int notes_cursor() { return s_cursor; }
void notes_set_cursor(int idx) {
  if (s_n == 0) { s_cursor = 0; return; }
  if (idx < 0) idx = s_n + (idx % s_n);
  s_cursor = idx % s_n;
}
