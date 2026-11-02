#include "cam.h"
#include <WiFi.h>
#include <lwip/sockets.h>
#include "esp_camera.h"

// ---- OV5640 引脚（官方原理图 V2，见 docs/引脚定义.md）----
#define CAM_PIN_PWDN   17
#define CAM_PIN_RESET  -1
#define CAM_PIN_XCLK    8
#define CAM_PIN_SIOD   21
#define CAM_PIN_SIOC   16
#define CAM_PIN_D7      2
#define CAM_PIN_D6      7
#define CAM_PIN_D5     10
#define CAM_PIN_D4     14
#define CAM_PIN_D3     11
#define CAM_PIN_D2     15
#define CAM_PIN_D1     13
#define CAM_PIN_D0     12
#define CAM_PIN_VSYNC   6
#define CAM_PIN_HREF    4
#define CAM_PIN_PCLK    9

namespace {

#define BOUNDARY "frameboundary"
const int  MAX_VIEWERS = 1;

WiFiServer s_srv(8080);
bool       s_on = false;
SemaphoreHandle_t s_mtx = nullptr;

volatile uint32_t s_frames = 0;
volatile float    s_fps = 0;
volatile int      s_viewers = 0;
uint32_t s_last_push_ms = 0;
uint32_t s_last_frame_ms = 0;
char     s_res[16] = "--";

struct Viewer {
  WiFiClient c;
  bool active = false;
};
Viewer s_vs[MAX_VIEWERS];

camera_fb_t *grab(uint32_t timeout_ms = 3000) {
  if (s_mtx && xSemaphoreTake(s_mtx, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) return nullptr;
  camera_fb_t *fb = esp_camera_fb_get();
  if (s_mtx) xSemaphoreGive(s_mtx);
  return fb;
}

void release(camera_fb_t *fb) {
  if (!fb) return;
  if (s_mtx) xSemaphoreTake(s_mtx, portMAX_DELAY);
  esp_camera_fb_return(fb);
  if (s_mtx) xSemaphoreGive(s_mtx);
}

// 允许部分写入：WiFiClient 缓冲满时返回部分长度不是错误
bool send_all(WiFiClient &c, const uint8_t *buf, size_t len, uint32_t timeout_ms) {
  size_t sent = 0;
  uint32_t t0 = millis();
  while (sent < len) {
    size_t n = c.write(buf + sent, len - sent);
    if (n > 0) { sent += n; continue; }
    if (!c.connected()) return false;
    if (millis() - t0 > timeout_ms) return false;
    delay(2);
  }
  return true;
}

// 丢弃旧连接：SO_LINGER(1,0) 让 TCP 立刻 RST，把 PCB 还给 lwIP（一共只有 10 个 socket）
void drop_client(WiFiClient &c) {
  if (!c) return;
  int fd = c.fd();
  if (fd >= 0) { struct linger lg = {1, 0}; setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg)); }
  c.stop();
}

void close_graceful(WiFiClient &c) { c.flush(); delay(5); c.stop(); }

void send_headers(WiFiClient &c, const char *ctype, int len) {
  c.printf("HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
           "Cache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\n"
           "Connection: close\r\n\r\n", ctype, len);
}

void accept_once() {
  WiFiClient nc = s_srv.available();
  if (!nc) return;

  String line;
  uint32_t t0 = millis();
  while (millis() - t0 < 250) {
    while (nc.available()) {
      char ch = (char)nc.read();
      if (ch == '\n') { if (line.endsWith("\r")) line.remove(line.length() - 1); goto got_line; }
      if (line.length() < 120) line += ch;
    }
    delay(1);
  }
got_line:
  while (nc.available()) nc.read();

  if (line.startsWith("GET /capture")) {
    camera_fb_t *fb = grab();
    if (fb) {
      send_headers(nc, "image/jpeg", fb->len);
      send_all(nc, fb->buf, fb->len, 800);
      release(fb);
      s_frames++;
    } else {
      nc.print("HTTP/1.1 503 Busy\r\nContent-Length: 0\r\n\r\n");
    }
    close_graceful(nc);
    return;
  }

  if (line.startsWith("GET /status.json")) {
    char b[256];
    const bool ap = (WiFi.getMode() == WIFI_AP);
    snprintf(b, sizeof(b),
             "{\"mode\":\"cam\",\"ip\":\"%s\",\"res\":\"%s\",\"fps\":%.1f,"
             "\"frames\":%lu,\"viewers\":%d,\"heap\":%lu}",
             (ap ? WiFi.softAPIP() : WiFi.localIP()).toString().c_str(),
             s_res, (double)s_fps, (unsigned long)s_frames, s_viewers,
             (unsigned long)ESP.getFreeHeap());
    send_headers(nc, "application/json", strlen(b));
    send_all(nc, (const uint8_t *)b, strlen(b), 500);
    close_graceful(nc);
    return;
  }

  if (line.startsWith("GET / ") || line.startsWith("GET /index")) {
    static const char page[] =
        "<!doctype html><meta charset=utf-8><title>ESP32 照相</title>"
        "<body style='margin:0;background:#111;color:#eee;font-family:sans-serif'>"
        "<div style='padding:10px 14px;font-size:15px'>ESP32-S3 照相模式</div>"
        "<img src='/stream' style='width:100%;max-width:800px;display:block'>"
        "<div style='padding:10px 14px;font-size:13px;color:#9aa'>"
        "<a style='color:#4da3ff' href='/capture'>取一张</a> · "
        "<a style='color:#4da3ff' href='/status.json'>状态</a></div></body>";
    send_headers(nc, "text/html; charset=utf-8", (int)strlen(page));
    send_all(nc, (const uint8_t *)page, strlen(page), 800);
    close_graceful(nc);
    return;
  }

  if (!line.startsWith("GET /stream")) {
    nc.print("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
    close_graceful(nc);
    return;
  }

  for (int i = 0; i < MAX_VIEWERS; i++) {
    if (s_vs[i].active) { drop_client(s_vs[i].c); s_vs[i].active = false; if (s_viewers > 0) s_viewers--; }
  }
  nc.setNoDelay(true);
  nc.printf("HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=" BOUNDARY "\r\n"
            "Cache-Control: no-store\r\nConnection: close\r\n\r\n");
  nc.flush();
  s_vs[0].c = nc;
  s_vs[0].active = true;
  s_viewers++;
  Serial.printf("[CAM] 观看端接入 现%d\n", s_viewers);
}

void push_frame() {
  if (s_viewers == 0) return;
  if (millis() - s_last_push_ms < 40) return;   // ~25fps 上限

  camera_fb_t *fb = grab();
  if (!fb) return;

  static const char *hdr = "\r\n--" BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: ";
  char lenbuf[24];
  int n = snprintf(lenbuf, sizeof(lenbuf), "%u\r\n\r\n", (unsigned)fb->len);

  for (int i = 0; i < MAX_VIEWERS; i++) {
    if (!s_vs[i].active) continue;
    bool conn = s_vs[i].c.connected();
    bool ok = conn && send_all(s_vs[i].c, (const uint8_t *)hdr, strlen(hdr), 400) &&
              send_all(s_vs[i].c, (const uint8_t *)lenbuf, n, 400) &&
              send_all(s_vs[i].c, fb->buf, fb->len, 400);
    if (!ok) {
      drop_client(s_vs[i].c);
      s_vs[i].active = false;
      if (s_viewers > 0) s_viewers--;
      Serial.println("[CAM] 观看端断开");
    }
  }
  release(fb);

  uint32_t now = millis();
  s_frames++;
  if (s_last_frame_ms) {
    float inst = 1000.0f / (now - s_last_frame_ms + 1);
    s_fps = s_fps * 0.85f + inst * 0.15f;
  }
  s_last_frame_ms = now;
  s_last_push_ms = now;
}

}  // namespace

bool cam_begin() {
  if (s_on) return true;

  // LEDC 通道要和背光错开：背光用 channel 0 / timer 0
  camera_config_t cfg = {};
  cfg.ledc_channel = LEDC_CHANNEL_1;
  cfg.ledc_timer   = LEDC_TIMER_1;
  cfg.pin_pwdn  = CAM_PIN_PWDN;
  cfg.pin_reset = CAM_PIN_RESET;
  cfg.pin_xclk  = CAM_PIN_XCLK;
  cfg.pin_sccb_sda = CAM_PIN_SIOD;
  cfg.pin_sccb_scl = CAM_PIN_SIOC;
  cfg.pin_d7 = CAM_PIN_D7; cfg.pin_d6 = CAM_PIN_D6;
  cfg.pin_d5 = CAM_PIN_D5; cfg.pin_d4 = CAM_PIN_D4;
  cfg.pin_d3 = CAM_PIN_D3; cfg.pin_d2 = CAM_PIN_D2;
  cfg.pin_d1 = CAM_PIN_D1; cfg.pin_d0 = CAM_PIN_D0;
  cfg.pin_vsync = CAM_PIN_VSYNC;
  cfg.pin_href  = CAM_PIN_HREF;
  cfg.pin_pclk  = CAM_PIN_PCLK;
  cfg.xclk_freq_hz = 20000000;
  cfg.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    cfg.frame_size   = FRAMESIZE_VGA;
    cfg.jpeg_quality = 12;
    cfg.fb_count     = 2;
    cfg.fb_location  = CAMERA_FB_IN_PSRAM;
    cfg.grab_mode    = CAMERA_GRAB_LATEST;
  } else {
    cfg.frame_size   = FRAMESIZE_QVGA;
    cfg.jpeg_quality = 15;
    cfg.fb_count     = 1;
    cfg.fb_location  = CAMERA_FB_IN_DRAM;
    cfg.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
  }

  esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) {
    Serial.printf("[CAM] 初始化失败 0x%x (%s)\n", err, esp_err_to_name(err));
    return false;
  }
  sensor_t *s = esp_camera_sensor_get();
  snprintf(s_res, sizeof(s_res), "%dx%d",
           resolution[s->status.framesize].width, resolution[s->status.framesize].height);
  Serial.printf("[CAM] PID=0x%02X 图像 %s\n", s->id.PID, s_res);

  if (!s_mtx) s_mtx = xSemaphoreCreateMutex();
  s_srv.begin();
  s_srv.setNoDelay(true);
  s_on = true;
  s_fps = 0; s_frames = 0; s_viewers = 0; s_last_frame_ms = 0;
  return true;
}

void cam_end() {
  if (!s_on) return;
  for (int i = 0; i < MAX_VIEWERS; i++) {
    if (s_vs[i].active) { drop_client(s_vs[i].c); s_vs[i].active = false; }
  }
  s_viewers = 0;
  s_srv.end();
  esp_camera_deinit();
  s_on = false;
  Serial.println("[CAM] 已停止");
}

void cam_loop() {
  if (!s_on) return;
  accept_once();
  push_frame();
}

bool       cam_active()   { return s_on; }
float      cam_fps()      { return s_fps; }
int        cam_viewers()  { return s_viewers; }
uint32_t   cam_frames()   { return s_frames; }
const char *cam_res()     { return s_res; }
