// ESP32-S3-LCD-2 网页图传（v5）
// 结构：80 端口 = 控制页（HTTP 服务，响应快）；81 端口 = MJPEG 流（独立 TCP 服务）
//
// v5 变更（关键修复）：
//   ESP-IDF 4.4 的 httpd 是单任务模型 —— 一个 handler 阻塞（MJPEG 无限推流）整个服务就卡死，
//   所以 v1~v4 期间"看图时点任何按钮都没反应"。这里把流搬到 81 端口用 WiFiServer 自己带，
//   80 端口的控制请求永远有空处理。
//   同时：摄像头加互斥锁；无卡时 /shoot 不抢摄像头；每个请求打串口日志。

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <SPI.h>
#include <SD.h>
#include <esp_camera.h>
#include <esp_http_server.h>
#include <Arduino_GFX_Library.h>
#include <stdarg.h>

#include "camera_pins.h"
#include "wifi_config.h"
#include "www.h"

// ---------------- 状态 ----------------
static httpd_handle_t   s_server = nullptr;
static WiFiServer       s_stream_srv(8080);
static SemaphoreHandle_t s_cam_mtx = nullptr;

static volatile uint32_t s_frame_count = 0;   // 累计送出帧数
static volatile float    s_fps = 0;
static volatile int      s_viewers = 0;       // 当前流客户端数
static bool   s_sd_ok = false;
static char   s_res[16] = "--";
static uint32_t s_last_frame_ms = 0;
static uint32_t s_last_push_ms = 0;

#define BOUNDARY "frameboundary"

static void diag(const char *fmt, ...);   // 前向声明（事件日志）
#define MAX_VIEWERS 2

struct Viewer {
  WiFiClient c;
  bool  active = false;
  uint32_t last_ok = 0;   // 最近一次写成功的时刻，用来淘汰僵尸连接
};

static Viewer s_vs[MAX_VIEWERS];

// ---------------- 摄像头（互斥） ----------------
static camera_fb_t *cam_grab(uint32_t timeout_ms = 3000) {
  if (s_cam_mtx && xSemaphoreTake(s_cam_mtx, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
    Serial.println("[CAM] 取帧超时");
    return nullptr;
  }
  camera_fb_t *fb = esp_camera_fb_get();
  if (s_cam_mtx) xSemaphoreGive(s_cam_mtx);
  return fb;
}

static void cam_release(camera_fb_t *fb) {
  if (!fb) return;
  if (s_cam_mtx) xSemaphoreTake(s_cam_mtx, portMAX_DELAY);
  esp_camera_fb_return(fb);
  if (s_cam_mtx) xSemaphoreGive(s_cam_mtx);
}

static bool init_camera() {
  camera_config_t cfg = {};
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer   = LEDC_TIMER_0;

  cfg.pin_pwdn = CAM_PIN_PWDN;
  cfg.pin_reset = CAM_PIN_RESET;
  cfg.pin_xclk = CAM_PIN_XCLK;
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
    Serial.printf("[CAM] 初始化失败: 0x%x (%s)\n", err, esp_err_to_name(err));
    return false;
  }
  sensor_t *s = esp_camera_sensor_get();
  snprintf(s_res, sizeof(s_res), "%dx%d",
           resolution[s->status.framesize].width,
           resolution[s->status.framesize].height);
  Serial.printf("[CAM] 传感器 PID=0x%02X VER=0x%02X，图像 %s\n",
                s->id.PID, s->id.VER, s_res);
  return true;
}

// ---------------- TF 卡 ----------------
static SPIClass s_sd_spi(HSPI);

static void init_sd() {
  s_sd_spi.begin(39, 40, 38, 41);
  s_sd_ok = SD.begin(41, s_sd_spi, 20000000);
  if (!s_sd_ok) { Serial.println("[SD] 未检测到卡（不影响图传）"); return; }
  Serial.printf("[SD] 卡已挂载，容量 %.0f MB\n", SD.cardSize() / (1024.0 * 1024.0));
  if (!SD.exists("/photo")) SD.mkdir("/photo");
}

// ---------------- Wi-Fi ----------------
static void start_mdns() {
  if (MDNS.begin(MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[mDNS] http://%s.local/\n", MDNS_NAME);
  } else {
    Serial.println("[mDNS] 启动失败（用 IP 访问即可）");
  }
}

static void start_wifi() {
  WiFi.persistent(false);
  if (strlen(WIFI_SSID) > 0) {
    Serial.printf("[WiFi] 连接 %s ...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(MDNS_NAME);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
      delay(300);
      Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[WiFi] 已连接，IP: %s，网关 %s，信号 %d dBm\n",
                    WiFi.localIP().toString().c_str(),
                    WiFi.gatewayIP().toString().c_str(), WiFi.RSSI());
      start_mdns();
      return;
    }
    Serial.println("[WiFi] 连接超时，改用热点模式");
  }
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("[WiFi] 热点已开: %s / %s\n", AP_SSID, AP_PASS);
  start_mdns();
}

static IPAddress cur_ip() {
  return (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP() : WiFi.localIP();
}

// ---------------- 屏幕（ST7789T3，240x320，SPI） ----------------
// 引脚（官方原理图）：SCLK 39 / MOSI 38 / DC 42 / CS 45 / RST 未接 / BL 1
#define LCD_BL_PIN 1
static Arduino_DataBus *s_bus = nullptr;
static Arduino_GFX     *s_gfx = nullptr;

static void lcd_init() {
  // 背光 PWM（5000Hz / 10bit）
  ledcSetup(0, 5000, 10);
  ledcAttachPin(LCD_BL_PIN, 0);
  ledcWrite(0, 1023);

  s_bus = new Arduino_ESP32SPI(42 /*DC*/, 45 /*CS*/, 39 /*SCK*/, 38 /*MOSI*/,
                               GFX_NOT_DEFINED /*MISO*/);
  s_gfx = new Arduino_ST7789(s_bus, GFX_NOT_DEFINED /*RST*/, 1 /*rotation 横屏*/,
                             true /*IPS*/, 240, 320);
  if (!s_gfx->begin(40000000)) {
    Serial.println("[LCD] 初始化失败");
    delete s_gfx; s_gfx = nullptr;
    return;
  }
  s_gfx->fillScreen(BLACK);
  s_gfx->setTextColor(WHITE);
  s_gfx->setTextSize(2);
  s_gfx->setCursor(10, 10);
  s_gfx->println("BOOTING...");
  Serial.println("[LCD] ST7789 就绪（320x240 横屏）");
}

static void lcd_status() {
  if (!s_gfx) return;
  const bool ap = (WiFi.getMode() == WIFI_AP);
  IPAddress ip = cur_ip();

  s_gfx->fillScreen(BLACK);
  s_gfx->setTextSize(2);
  s_gfx->setTextColor(0x07E0);                 // 绿
  s_gfx->setCursor(8, 6);
  s_gfx->print("ESP32-S3 CAM");
  s_gfx->drawFastHLine(0, 28, 320, 0x3186);

  s_gfx->setTextColor(WHITE);
  s_gfx->setCursor(8, 38);
  if (ap) {
    s_gfx->setTextColor(0xFD20);               // 橙：热点模式
    s_gfx->print("AP MODE");
    s_gfx->setTextColor(WHITE);
    s_gfx->setTextSize(3);
    s_gfx->setCursor(8, 64);
    s_gfx->print(ip.toString());
    s_gfx->setTextSize(2);
    s_gfx->setCursor(8, 100);
    s_gfx->print(AP_SSID);
  } else {
    s_gfx->setTextSize(3);
    s_gfx->print(ip.toString());
    s_gfx->setTextSize(2);
    s_gfx->setCursor(8, 74);
    s_gfx->printf("%s.local", MDNS_NAME);
  }

  s_gfx->drawFastHLine(0, 112, 320, 0x3186);
  s_gfx->setCursor(8, 122);
  s_gfx->printf("WiFi  %d dBm", ap ? 0 : WiFi.RSSI());
  s_gfx->setCursor(8, 146);
  s_gfx->printf("%s  %.1f fps", s_res, (double)s_fps);
  s_gfx->setCursor(8, 170);
  s_gfx->printf("Viewers %d  Frames %lu", s_viewers, (unsigned long)s_frame_count);
  s_gfx->setCursor(8, 194);
  s_gfx->printf("SD   %s", s_sd_ok ? "OK" : "none");
  s_gfx->setCursor(8, 218);
  s_gfx->printf("Heap %lu KB", (unsigned long)(ESP.getFreeHeap() / 1024));
  s_gfx->setCursor(8, 242);
  s_gfx->printf("Up   %lu s", (unsigned long)(millis() / 1000));
}

// ---------------- 81 端口：MJPEG ----------------
static void stream_accept() {
  WiFiClient nc = s_stream_srv.available();
  if (!nc) return;

  // 读掉请求头（最多等 300ms），不必解析
  uint32_t t0 = millis();
  while (nc.available() == 0 && millis() - t0 < 300) delay(2);
  while (nc.available()) nc.read();

  // 找空位；满了就淘汰"最久没写成功"的那个（刷新页面会留下僵尸连接）
  int slot = -1;
  for (int i = 0; i < MAX_VIEWERS; i++) {
    if (!s_vs[i].active) { slot = i; break; }
  }
  if (slot < 0) {
    uint32_t oldest = 0xFFFFFFFF;
    for (int i = 0; i < MAX_VIEWERS; i++) {
      if (s_vs[i].last_ok < oldest) { oldest = s_vs[i].last_ok; slot = i; }
    }
    diag("淘汰 s%d", slot); Serial.printf("[流] 槽位已满，淘汰旧连接 #%d\n", slot);
    s_vs[slot].c.stop();
    s_vs[slot].active = false;
    s_viewers--;
  }

  nc.setNoDelay(true);
  nc.printf(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: multipart/x-mixed-replace; boundary=" BOUNDARY "\r\n"
      "Cache-Control: no-store\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "Connection: close\r\n\r\n");
  nc.flush();

  s_vs[slot].c = nc;
  s_vs[slot].active = true;
  s_vs[slot].last_ok = millis();
  s_viewers++;
  diag("接入 s%d n%d", slot, s_viewers); Serial.printf("[流] 接入 槽位%d 现 %d 个 堆%luKB\n", slot, s_viewers,
                (unsigned long)(ESP.getFreeHeap() / 1024));
}

// 完整写出一段数据：允许分段写入（WiFiClient 缓冲满时会返回部分长度，这不是错误）
static bool send_all(WiFiClient &c, const uint8_t *buf, size_t len, uint32_t timeout_ms = 1200) {
  size_t sent = 0;
  uint32_t t0 = millis();
  while (sent < len) {
    size_t n = c.write(buf + sent, len - sent);
    if (n > 0) { sent += n; continue; }
    if (!c.connected()) return false;              // 对端真断了
    if (millis() - t0 > timeout_ms) return false;  // 太慢，判定跟不上
    delay(2);
  }
  return true;
}

static void stream_push() {
  if (s_viewers == 0) return;
  if (millis() - s_last_push_ms < 40) return;   // 限速 ~25fps，别把 AP 打满

  camera_fb_t *fb = cam_grab();
  if (!fb) return;

  static const char *hdr =
      "\r\n--" BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: ";
  char lenbuf[24];
  int n = snprintf(lenbuf, sizeof(lenbuf), "%u\r\n\r\n", (unsigned)fb->len);

  for (int i = 0; i < MAX_VIEWERS; i++) {
    if (!s_vs[i].active) continue;

    bool conn = s_vs[i].c.connected();
    bool ok = conn &&
              send_all(s_vs[i].c, (const uint8_t *)hdr, strlen(hdr)) &&
              send_all(s_vs[i].c, (const uint8_t *)lenbuf, n) &&
              send_all(s_vs[i].c, fb->buf, fb->len);

    if (!ok) {
      s_vs[i].c.stop();
      s_vs[i].active = false;
      s_viewers--;
      diag("断开 s%d %s", i, conn ? "wfail" : "peer"); Serial.printf("[流] 断开 槽位%d 原因=%s 剩%d 堆%luKB\n", i,
                    conn ? "写超时/写失败" : "对端已断", s_viewers,
                    (unsigned long)(ESP.getFreeHeap() / 1024));
    } else {
      s_vs[i].last_ok = millis();
    }
  }

  cam_release(fb);
  s_frame_count++;
  s_last_push_ms = millis();

  uint32_t now = millis();
  if (s_last_frame_ms) {
    float inst = 1000.0f / (now - s_last_frame_ms + 1);
    s_fps = s_fps * 0.85f + inst * 0.15f;
  }
  s_last_frame_ms = now;
}

// ---------------- 事件日志（给 /diag 用） ----------------
#define DIAG_N 14
static char s_diag[DIAG_N][64];
static int  s_diag_idx = 0;
static int  s_diag_cnt = 0;

static void diag(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char tmp[64];
  int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  if (n < 0) return;
  snprintf(s_diag[s_diag_idx], sizeof(s_diag[0]), "%6lus %s",
           (unsigned long)(millis() / 1000), tmp);
  s_diag_idx = (s_diag_idx + 1) % DIAG_N;
  if (s_diag_cnt < DIAG_N) s_diag_cnt++;
}

// ---------------- 80 端口：控制 ----------------
static void log_req(httpd_req_t *req) {
  Serial.printf("[HTTP] %s %s\n", req->method == HTTP_GET ? "GET" : "OTHER", req->uri);
}

static esp_err_t h_index(httpd_req_t *req) {
  log_req(req);
  esp_err_t res = httpd_resp_set_type(req, "text/html; charset=utf-8");
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");

  const bool ap = (WiFi.getMode() == WIFI_AP);
  IPAddress ip = cur_ip();
  char b[640], rssiTxt[24];
  if (ap) snprintf(rssiTxt, sizeof(rssiTxt), "热点模式");
  else    snprintf(rssiTxt, sizeof(rssiTxt), "%d dBm", ap ? 0 : WiFi.RSSI());

  res = httpd_resp_send_chunk(req, PAGE_HEAD, strlen(PAGE_HEAD));
  if (res == ESP_OK) {
    snprintf(b, sizeof(b),
             "<span class=\"pill\">%s · %s</span><span class=\"pill warn\">%s</span>",
             ap ? "AP" : "STA", ip.toString().c_str(), rssiTxt);
    res = httpd_resp_send_chunk(req, b, strlen(b));
  }
  // 视频流地址：8080 端口，IP 由服务端注入（不依赖 JS）
  if (res == ESP_OK) {
    snprintf(b, sizeof(b),
             "</header><main><div class=\"viewer\">"
             "<img id=\"stream\" src=\"http://%s:8080/stream\" "
             "data-src=\"http://%s:8080/stream\" alt=\"视频流\"></div>"
             "<p class=\"note\" style=\"margin:6px 0 0\">画面没出来？直接打开 "
             "<a href=\"http://%s:8080/stream\" style=\"color:#79c0ff\">http://%s:8080/stream</a>"
             " 试试；拉不动就走低配模式 <a href=\"/simple\" style=\"color:#79c0ff\">/simple</a>。</p>"
             "<div class=\"meta\">",
             ip.toString().c_str(), ip.toString().c_str(),
             ip.toString().c_str(), ip.toString().c_str());
    res = httpd_resp_send_chunk(req, b, strlen(b));
  }
  if (res == ESP_OK) {
    snprintf(b, sizeof(b),
             "<span>分辨率 <b>%s</b></span>"
             "<span>帧率 <b>%.1f</b></span>"
             "<span>已传 <b>%lu</b> 帧</span>"
             "<span>观看端 <b>%d</b></span>"
             "<span>堆 <b>%lu KB</b></span>"
             "<span>运行 <b>%lu s</b></span>"
             "<span>TF 卡 <b>%s</b></span>",
             s_res, (double)s_fps, (unsigned long)s_frame_count, s_viewers,
             (unsigned long)(ESP.getFreeHeap() / 1024),
             (unsigned long)(millis() / 1000),
             s_sd_ok ? "已挂载" : "未插卡");
    res = httpd_resp_send_chunk(req, b, strlen(b));
  }
  if (res == ESP_OK) res = httpd_resp_send_chunk(req, PAGE_TAIL, strlen(PAGE_TAIL));
  if (res == ESP_OK) res = httpd_resp_send_chunk(req, NULL, 0);
  return res;
}

// 80 端口的 /stream 只做跳转提示（老链接不报错）
static esp_err_t h_stream_hint(httpd_req_t *req) {
  log_req(req);
  char b[160];
  snprintf(b, sizeof(b), "视频流在 http://%s:8080/stream",
           cur_ip().toString().c_str());
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  return httpd_resp_send(req, b, HTTPD_RESP_USE_STRLEN);
}

// 低配预览：不依赖 MJPEG（个别浏览器/扩展会拦 :8080 的 multipart 流）
static esp_err_t h_simple(httpd_req_t *req) {
  log_req(req);
  char b[700];
  snprintf(b, sizeof(b),
           "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
           "<meta http-equiv=\"refresh\" content=\"1\">"
           "<title>低配预览</title></head>"
           "<body style=\"margin:0;background:#111418\">"
           "<img src=\"/capture\" style=\"display:block;width:100%%\">"
           "<p style=\"color:#8b949e;font:13px system-ui;text-align:center;margin:10px\">"
           "低配模式：每秒刷新一帧（不走 MJPEG，兼容性最好）· "
           "<a href=\"/\" style=\"color:#79c0ff\">返回图传</a></p>"
           "</body></html>");
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, b, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_capture(httpd_req_t *req) {
  log_req(req);
  camera_fb_t *fb = cam_grab();
  if (!fb) return httpd_resp_send_500(req);
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=snap.jpg");
  esp_err_t r = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  cam_release(fb);
  return r;
}

static esp_err_t h_shoot(httpd_req_t *req) {
  log_req(req);
  char msg[200];
  if (!s_sd_ok) {
    snprintf(msg, sizeof(msg), "没有 TF 卡：先插卡再试");
  } else {
    camera_fb_t *fb = cam_grab();
    if (!fb) {
      snprintf(msg, sizeof(msg), "抓帧超时，过一秒再点");
    } else {
      char path[64];
      snprintf(path, sizeof(path), "/photo/%lu.jpg", (unsigned long)millis());
      File f = SD.open(path, FILE_WRITE);
      if (!f) snprintf(msg, sizeof(msg), "写卡失败：%s", path);
      else {
        size_t n = f.write(fb->buf, fb->len);
        f.close();
        snprintf(msg, sizeof(msg), "已保存 %s（%.1f KB）", path, n / 1024.0);
      }
      cam_release(fb);
    }
  }
  Serial.printf("[拍照] %s\n", msg);
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_send_chunk(req, RESULT_HEAD, strlen(RESULT_HEAD));
  httpd_resp_send_chunk(req, msg, strlen(msg));
  httpd_resp_send_chunk(req, RESULT_TAIL, strlen(RESULT_TAIL));
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t h_status_html(httpd_req_t *req) {
  log_req(req);
  char b[900];
  const bool ap = (WiFi.getMode() == WIFI_AP);
  snprintf(b, sizeof(b),
           "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
           "<title>状态</title><style>body{background:#111418;color:#e6e9ef;"
           "font:15px/1.8 system-ui,\"Microsoft YaHei\",sans-serif;padding:24px}"
           "table{border-collapse:collapse;width:100%%}td{padding:6px 10px;"
           "border-bottom:1px solid #232a33}td:first-child{color:#9aa4b2;width:42%%}"
           "a{color:#79c0ff}</style></head><body><h3>板子状态</h3><table>"
           "<tr><td>网络</td><td>%s · %s</td></tr>"
           "<tr><td>信号</td><td>%d dBm</td></tr>"
           "<tr><td>分辨率</td><td>%s</td></tr>"
           "<tr><td>帧率 / 累计</td><td>%.1f fps / %lu 帧</td></tr>"
           "<tr><td>观看端</td><td>%d</td></tr>"
           "<tr><td>空闲堆</td><td>%lu KB</td></tr>"
           "<tr><td>运行</td><td>%lu s</td></tr>"
           "<tr><td>TF 卡</td><td>%s</td></tr></table>"
           "<p><a href=\"/status.json\">JSON</a> · <a href=\"/\">返回图传</a></p>"
           "</body></html>",
           ap ? "AP" : "STA", cur_ip().toString().c_str(), ap ? 0 : WiFi.RSSI(),
           s_res, (double)s_fps, (unsigned long)s_frame_count, s_viewers,
           (unsigned long)(ESP.getFreeHeap() / 1024), (unsigned long)(millis() / 1000),
           s_sd_ok ? "已挂载" : "未插卡");
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, b, HTTPD_RESP_USE_STRLEN);
}

// 事件日志页：/diag —— 流接入/断开/淘汰都会记在这里，浏览器里直接看
static esp_err_t h_diag(httpd_req_t *req) {
  log_req(req);
  char b[1500];
  int off = snprintf(b, sizeof(b),
           "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
           "<meta http-equiv=\"refresh\" content=\"3\">"
           "<title>事件日志</title><style>body{background:#111418;color:#e6e9ef;"
           "font:14px/1.7 ui-monospace,Consolas,monospace;padding:18px}"
           "h3{font:600 15px system-ui}a{color:#79c0ff}</style></head><body>"
           "<h3>最近 %d 条流事件（3 秒自动刷新）</h3><pre>", s_diag_cnt);
  for (int k = 0; k < s_diag_cnt; k++) {
    int idx = (s_diag_idx - s_diag_cnt + k + DIAG_N * 2) % DIAG_N;
    off += snprintf(b + off, sizeof(b) - off, "%s\n", s_diag[idx]);
    if (off > (int)sizeof(b) - 80) break;
  }
  off += snprintf(b + off, sizeof(b) - off,
           "</pre><p>观看端 %d · 帧 %lu · 堆 %lu KB · 运行 %lu s</p>"
           "<p><a href=\"/\">返回图传</a> · <a href=\"/status\">状态</a></p>"
           "</body></html>",
           s_viewers, (unsigned long)s_frame_count,
           (unsigned long)(ESP.getFreeHeap() / 1024), (unsigned long)(millis() / 1000));
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, b, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_status_json(httpd_req_t *req) {
  log_req(req);
  char b[340];
  const bool ap = (WiFi.getMode() == WIFI_AP);
  snprintf(b, sizeof(b),
           "{\"mode\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"res\":\"%s\","
           "\"fps\":%.1f,\"frames\":%lu,\"viewers\":%d,\"heap\":%lu,\"uptime\":%lu,\"sd\":%s,\"lcd\":%s}",
           ap ? "AP" : "STA", cur_ip().toString().c_str(), ap ? 0 : WiFi.RSSI(),
           s_res, (double)s_fps, (unsigned long)s_frame_count, s_viewers,
           (unsigned long)ESP.getFreeHeap(), (unsigned long)millis(),
           (s_sd_ok ? "true" : "false"), (s_gfx ? "true" : "false"));
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, b, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_favicon(httpd_req_t *req) {
  httpd_resp_set_status(req, "204 No Content");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t h_restart(httpd_req_t *req) {
  log_req(req);
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_send_chunk(req, RESULT_HEAD, strlen(RESULT_HEAD));
  httpd_resp_send_chunk(req, "正在重启，3 秒后刷新", 33);
  httpd_resp_send_chunk(req, RESULT_TAIL, strlen(RESULT_TAIL));
  httpd_resp_send_chunk(req, NULL, 0);
  delay(300);
  ESP.restart();
  return ESP_OK;
}

static void start_http() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port      = 80;
  cfg.ctrl_port        = 32768;
  cfg.max_uri_handlers = 10;
  cfg.max_open_sockets = 4;      // lwIP 一共 10 个 socket，流服务还要占
  cfg.backlog_conn     = 3;
  cfg.lru_purge_enable = true;
  cfg.stack_size       = 8192;
  cfg.recv_wait_timeout = 5;
  cfg.send_wait_timeout = 5;

  if (httpd_start(&s_server, &cfg) != ESP_OK) {
    Serial.println("[HTTP] 服务启动失败");
    return;
  }
  httpd_uri_t uris[] = {
      {"/",            HTTP_GET, h_index,       nullptr},
      {"/stream",      HTTP_GET, h_stream_hint, nullptr},
      {"/capture",     HTTP_GET, h_capture,     nullptr},
      {"/shoot",       HTTP_GET, h_shoot,       nullptr},
      {"/simple",      HTTP_GET, h_simple,      nullptr},
      {"/status",      HTTP_GET, h_status_html, nullptr},
      {"/status.json", HTTP_GET, h_status_json, nullptr},
      {"/diag",        HTTP_GET, h_diag,        nullptr},
      {"/favicon.ico", HTTP_GET, h_favicon,     nullptr},
      {"/restart",     HTTP_GET, h_restart,     nullptr},
  };
  for (auto &u : uris) {
    esp_err_t e = httpd_register_uri_handler(s_server, &u);
    if (e != ESP_OK) Serial.printf("[HTTP] 注册 %s 失败 %d\n", u.uri, e);
  }
  Serial.println("[HTTP] 控制服务 80 端口就绪");
}

// ---------------- 自检 ----------------
static void self_test() {
  if (s_viewers > 0) { Serial.println("[自检] 有观看端，跳过"); return; }
  IPAddress ip = cur_ip();
  const char *paths[] = {"/", "/status", "/status.json"};
  for (auto p : paths) {
    WiFiClient c;
    if (!c.connect(ip, 80, 2000)) { Serial.printf("[自检] %s 连接失败\n", p); continue; }
    c.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", p, ip.toString().c_str());
    String resp; uint32_t t0 = millis();
    while (millis() - t0 < 2500) {
      while (c.available()) resp += (char)c.read();
      if (!c.connected() && !c.available()) break;
      delay(2);
    }
    c.stop();
    int sp = resp.indexOf(' ');
    Serial.printf("[自检] %-12s %s  %u 字节\n", p,
                  sp > 0 ? resp.substring(sp + 1, sp + 4).c_str() : "??",
                  (unsigned)resp.length());
  }
}

// ---------------- 主流程 ----------------
void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\n=== ESP32-S3-LCD-2 网页图传 v5 ===");
  Serial.printf("芯片 %s rev%d, PSRAM %s, 空闲堆 %lu KB\n",
                ESP.getChipModel(), ESP.getChipRevision(),
                psramFound() ? "有" : "无", (unsigned long)(ESP.getFreeHeap() / 1024));

  s_cam_mtx = xSemaphoreCreateMutex();
  lcd_init();
  init_sd();
  if (!init_camera()) Serial.println("[CAM] 摄像头不可用，仅提供状态页");
  start_wifi();
  lcd_status();

  s_stream_srv.begin();
  s_stream_srv.setNoDelay(true);
  Serial.println("[流] 端口 8080 就绪");

  start_http();
  Serial.printf("\n浏览器打开: http://%s/\n\n", cur_ip().toString().c_str());
  delay(300);
  self_test();
}

void loop() {
  stream_accept();
  stream_push();

  static uint32_t last_hb = 0, last_self = 0;
  if (millis() - last_hb > 5000) {
    last_hb = millis();
    int sta = (WiFi.getMode() == WIFI_AP) ? WiFi.softAPgetStationNum() : 0;
    Serial.printf("[%lus] 帧:%lu fps:%.1f 观看:%d 已连设备:%d 堆:%luKB\n",
                  millis() / 1000, (unsigned long)s_frame_count, (double)s_fps,
                  s_viewers, sta, (unsigned long)(ESP.getFreeHeap() / 1024));
  }
  if (millis() - last_self > 60000) {
    last_self = millis();
    if (s_server) self_test();
  }

  // 屏幕每 3 秒刷新一次
  static uint32_t last_lcd = 0;
  if (millis() - last_lcd > 3000) {
    last_lcd = millis();
    lcd_status();
  }

  delay(5);
}







