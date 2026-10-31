// ESP32-S3-LCD-2 网页图传（v3）
// 功能：OV5640 抓帧 → 浏览器 MJPEG 实时预览 + 单帧下载 + 拍照存 TF 卡 + 状态页
// 板子：微雪 Waveshare ESP32-S3-LCD-2（8MB PSRAM / 16MB Flash / ST7789T3 屏 / OV5640）
//
// v3 变更：
//   1. 摄像头访问加互斥锁 —— 图传与 /capture、/shoot 并发时不再互相卡死
//   2. /shoot 先判卡：没有 TF 卡就直接返回结果页，不去抢摄像头
//   3. /status 默认返回 HTML（任何浏览器都能看），/status.json 给工具用
//   4. 每个请求都往串口打一行日志，便于定位"点了没反应"

#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
#include <esp_camera.h>
#include <esp_http_server.h>

#include "camera_pins.h"
#include "wifi_config.h"
#include "www.h"

// ---------------- 全局状态 ----------------
static httpd_handle_t s_server = nullptr;
static SemaphoreHandle_t s_cam_mtx = nullptr;   // 摄像头互斥
static volatile uint32_t s_frame_count = 0;
static volatile float    s_fps = 0;
static bool   s_sd_ok = false;
static char   s_res[16] = "--";
static uint32_t s_last_frame_ms = 0;

#define BOUNDARY "frameboundary"

// ---------------- 摄像头（加锁访问） ----------------
static camera_fb_t *cam_grab(uint32_t timeout_ms = 3000) {
  if (s_cam_mtx && xSemaphoreTake(s_cam_mtx, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
    Serial.println("[CAM] 取帧超时（有别的请求正在用摄像头）");
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

  cfg.pin_pwdn  = CAM_PIN_PWDN;
  cfg.pin_reset = CAM_PIN_RESET;
  cfg.pin_xclk  = CAM_PIN_XCLK;
  cfg.pin_sccb_sda = CAM_PIN_SIOD;
  cfg.pin_sccb_scl = CAM_PIN_SIOC;

  cfg.pin_d7 = CAM_PIN_D7;
  cfg.pin_d6 = CAM_PIN_D6;
  cfg.pin_d5 = CAM_PIN_D5;
  cfg.pin_d4 = CAM_PIN_D4;
  cfg.pin_d3 = CAM_PIN_D3;
  cfg.pin_d2 = CAM_PIN_D2;
  cfg.pin_d1 = CAM_PIN_D1;
  cfg.pin_d0 = CAM_PIN_D0;
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
    Serial.println("[CAM] 0x105 = ESP_ERR_NOT_FOUND → FPC 没插好或朝向反了");
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
// 本板 TF 走 SPI，与 LCD 共用总线：39=CLK 38=MOSI 40=MISO 41=CS
static SPIClass s_sd_spi(HSPI);

static void init_sd() {
  s_sd_spi.begin(39, 40, 38, 41);
  s_sd_ok = SD.begin(41, s_sd_spi, 20000000);
  if (!s_sd_ok) {
    Serial.println("[SD] 未检测到卡（不影响图传）");
    return;
  }
  Serial.printf("[SD] 卡已挂载，容量 %.0f MB\n", SD.cardSize() / (1024.0 * 1024.0));
  if (!SD.exists("/photo")) SD.mkdir("/photo");
}

// ---------------- Wi-Fi ----------------
static void start_wifi() {
  WiFi.persistent(false);
  if (strlen(WIFI_SSID) > 0) {
    Serial.printf("[WiFi] 连接 %s ...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
      delay(300);
      Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[WiFi] 已连接，IP: %s\n", WiFi.localIP().toString().c_str());
      return;
    }
    Serial.println("[WiFi] 连接超时，改用热点模式");
  }
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("[WiFi] 热点已开: %s / %s\n", AP_SSID, AP_PASS);
  Serial.printf("[WiFi] 浏览器访问 http://%s/\n", WiFi.softAPIP().toString().c_str());
}

// ---------------- 公共小工具 ----------------
static void log_req(httpd_req_t *req) {
  Serial.printf("[HTTP] %s %s\n",
                req->method == HTTP_GET ? "GET" : "OTHER", req->uri);
}

static IPAddress cur_ip() {
  return (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP() : WiFi.localIP();
}

// ---------------- 首页 ----------------
static esp_err_t h_index(httpd_req_t *req) {
  log_req(req);
  esp_err_t res = httpd_resp_set_type(req, "text/html; charset=utf-8");
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");

  const bool ap = (WiFi.getMode() == WIFI_AP);
  IPAddress ip = cur_ip();
  int rssi = ap ? 0 : WiFi.RSSI();

  char b[640], rssiTxt[24];
  if (ap) snprintf(rssiTxt, sizeof(rssiTxt), "热点模式");
  else    snprintf(rssiTxt, sizeof(rssiTxt), "%d dBm", rssi);

  res = httpd_resp_send_chunk(req, PAGE_HEAD, strlen(PAGE_HEAD));
  if (res == ESP_OK) {
    snprintf(b, sizeof(b),
             "<span class=\"pill\">%s · %s</span><span class=\"pill warn\">%s</span>",
             ap ? "AP" : "STA", ip.toString().c_str(), rssiTxt);
    res = httpd_resp_send_chunk(req, b, strlen(b));
  }
  if (res == ESP_OK) res = httpd_resp_send_chunk(req, PAGE_MID, strlen(PAGE_MID));
  if (res == ESP_OK) {
    snprintf(b, sizeof(b),
             "<span>分辨率 <b>%s</b></span>"
             "<span>帧率 <b>%.1f</b></span>"
             "<span>已传 <b>%lu</b> 帧</span>"
             "<span>堆 <b>%lu KB</b></span>"
             "<span>运行 <b>%lu s</b></span>"
             "<span>TF 卡 <b>%s</b></span>",
             s_res, (double)s_fps,
             (unsigned long)s_frame_count,
             (unsigned long)(ESP.getFreeHeap() / 1024),
             (unsigned long)(millis() / 1000),
             s_sd_ok ? "已挂载" : "未插卡");
    res = httpd_resp_send_chunk(req, b, strlen(b));
  }
  if (res == ESP_OK) res = httpd_resp_send_chunk(req, PAGE_TAIL, strlen(PAGE_TAIL));
  if (res == ESP_OK) res = httpd_resp_send_chunk(req, NULL, 0);
  return res;
}

// ---------------- MJPEG 流 ----------------
static esp_err_t h_stream(httpd_req_t *req) {
  log_req(req);
  static const char *CT  = "multipart/x-mixed-replace;boundary=" BOUNDARY;
  static const char *BND = "\r\n--" BOUNDARY "\r\n";
  static const char *P1  = "Content-Type: image/jpeg\r\nContent-Length: ";
  static const char *P2  = "\r\n\r\n";

  esp_err_t res = httpd_resp_set_type(req, CT);
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "X-Framerate", "60");

  uint32_t served = 0;
  char head[64];
  while (true) {
    camera_fb_t *fb = cam_grab();
    if (!fb) { res = ESP_FAIL; break; }

    res = httpd_resp_send_chunk(req, BND, strlen(BND));
    if (res == ESP_OK) {
      int n = snprintf(head, sizeof(head), "%s%u%s", P1, (unsigned)fb->len, P2);
      res = httpd_resp_send_chunk(req, head, n);
    }
    if (res == ESP_OK && fb->format == PIXFORMAT_JPEG) {
      res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    }
    cam_release(fb);
    s_frame_count++;
    served++;

    uint32_t now = millis();
    if (s_last_frame_ms) {
      float inst = 1000.0f / (now - s_last_frame_ms + 1);
      s_fps = s_fps * 0.85f + inst * 0.15f;
    }
    s_last_frame_ms = now;

    if (res != ESP_OK) break;      // 浏览器断开
    delay(1);                      // 让出 CPU，别把别的请求饿死
  }
  Serial.printf("[HTTP] /stream 结束，本次输出 %lu 帧\n", (unsigned long)served);
  return res;
}

// ---------------- 单帧下载 ----------------
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

// ---------------- 拍照存卡 ----------------
static esp_err_t h_shoot(httpd_req_t *req) {
  log_req(req);
  char msg[200];
  snprintf(msg, sizeof(msg), "未知状态");

  if (!s_sd_ok) {
    // 没卡就别去抢摄像头，直接给结论
    snprintf(msg, sizeof(msg), "没有 TF 卡：先插卡再试");
  } else {
    camera_fb_t *fb = cam_grab();
    if (!fb) {
      snprintf(msg, sizeof(msg), "抓帧超时：图传正占着摄像头，过一秒再点");
    } else {
      char path[64];
      snprintf(path, sizeof(path), "/photo/%lu.jpg", (unsigned long)millis());
      File f = SD.open(path, FILE_WRITE);
      if (!f) {
        snprintf(msg, sizeof(msg), "写卡失败：%s", path);
      } else {
        size_t n = f.write(fb->buf, fb->len);
        f.close();
        snprintf(msg, sizeof(msg), "已保存 %s（%.1f KB）", path, n / 1024.0);
        Serial.println(msg);
      }
      cam_release(fb);
    }
  }

  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_send_chunk(req, RESULT_HEAD, strlen(RESULT_HEAD));
  httpd_resp_send_chunk(req, msg, strlen(msg));
  httpd_resp_send_chunk(req, RESULT_TAIL, strlen(RESULT_TAIL));
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

// ---------------- 状态：HTML ----------------
static esp_err_t h_status_html(httpd_req_t *req) {
  log_req(req);
  char b[900];
  const bool ap = (WiFi.getMode() == WIFI_AP);
  snprintf(b, sizeof(b),
           "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
           "<title>状态</title><style>body{background:#111418;color:#e6e9ef;"
           "font:15px/1.8 system-ui,\"Microsoft YaHei\",sans-serif;padding:24px;}"
           "table{border-collapse:collapse;width:100%%}td{padding:6px 10px;"
           "border-bottom:1px solid #232a33}td:first-child{color:#9aa4b2;width:40%%}"
           "a{color:#79c0ff}</style></head><body>"
           "<h3>板子状态</h3><table>"
           "<tr><td>网络模式</td><td>%s</td></tr>"
           "<tr><td>IP</td><td>%s</td></tr>"
           "<tr><td>信号</td><td>%d dBm</td></tr>"
           "<tr><td>分辨率</td><td>%s</td></tr>"
           "<tr><td>帧率 / 已传</td><td>%.1f fps / %lu 帧</td></tr>"
           "<tr><td>空闲堆</td><td>%lu KB</td></tr>"
           "<tr><td>运行时长</td><td>%lu s</td></tr>"
           "<tr><td>TF 卡</td><td>%s</td></tr>"
           "</table><p><a href=\"/status.json\">JSON 版本</a> · "
           "<a href=\"/\">← 返回图传</a></p></body></html>",
           ap ? "AP 热点" : "STA",
           cur_ip().toString().c_str(),
           ap ? 0 : WiFi.RSSI(),
           s_res, (double)s_fps, (unsigned long)s_frame_count,
           (unsigned long)(ESP.getFreeHeap() / 1024),
           (unsigned long)(millis() / 1000),
           s_sd_ok ? "已挂载" : "未插卡");
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, b, HTTPD_RESP_USE_STRLEN);
}

// ---------------- 状态：JSON ----------------
static esp_err_t h_status_json(httpd_req_t *req) {
  log_req(req);
  char b[320];
  const bool ap = (WiFi.getMode() == WIFI_AP);
  snprintf(b, sizeof(b),
           "{\"mode\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"res\":\"%s\","
           "\"fps\":%.1f,\"frames\":%lu,\"heap\":%lu,\"uptime\":%lu,\"sd\":%s}",
           ap ? "AP" : "STA", cur_ip().toString().c_str(), ap ? 0 : WiFi.RSSI(),
           s_res, (double)s_fps, (unsigned long)s_frame_count,
           (unsigned long)ESP.getFreeHeap(), (unsigned long)millis(),
           s_sd_ok ? "true" : "false");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, b, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_restart(httpd_req_t *req) {
  log_req(req);
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_send_chunk(req, RESULT_HEAD, strlen(RESULT_HEAD));
  httpd_resp_send_chunk(req, "正在重启，3 秒后刷新页面", 36);
  httpd_resp_send_chunk(req, RESULT_TAIL, strlen(RESULT_TAIL));
  httpd_resp_send_chunk(req, NULL, 0);
  delay(300);
  ESP.restart();
  return ESP_OK;
}

// ---------------- 启动 HTTP ----------------
static void start_http() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port      = 80;
  cfg.ctrl_port        = 32768;
  cfg.max_uri_handlers = 10;
  cfg.max_open_sockets = 10;
  cfg.backlog_conn     = 8;
  cfg.lru_purge_enable = true;
  cfg.stack_size       = 8192;

  if (httpd_start(&s_server, &cfg) != ESP_OK) {
    Serial.println("[HTTP] 服务启动失败");
    return;
  }
  httpd_uri_t uris[] = {
      {"/",           HTTP_GET, h_index,       nullptr},
      {"/stream",     HTTP_GET, h_stream,      nullptr},
      {"/capture",    HTTP_GET, h_capture,     nullptr},
      {"/shoot",      HTTP_GET, h_shoot,       nullptr},
      {"/status",     HTTP_GET, h_status_html, nullptr},
      {"/status.json",HTTP_GET, h_status_json, nullptr},
      {"/restart",    HTTP_GET, h_restart,     nullptr},
  };
  for (auto &u : uris) {
    esp_err_t e = httpd_register_uri_handler(s_server, &u);
    if (e != ESP_OK) Serial.printf("[HTTP] 注册 %s 失败: %d\n", u.uri, e);
  }
  Serial.println("[HTTP] 服务已启动，端口 80");
}

// ---------------- 自检：板子访问自己 ----------------
static void self_test() {
  IPAddress ip = cur_ip();
  struct { const char *path; } tests[] = {{"/"}, {"/status"}, {"/status.json"}};
  for (auto &t : tests) {
    WiFiClient c;
    if (!c.connect(ip, 80, 2000)) {
      Serial.printf("[自检] %s 连接失败\n", t.path);
      continue;
    }
    c.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", t.path, ip.toString().c_str());
    String resp;
    uint32_t t0 = millis();
    while (millis() - t0 < 2500) {
      while (c.available()) resp += (char)c.read();
      if (!c.connected() && !c.available()) break;
      delay(5);
    }
    c.stop();
    int sp = resp.indexOf(' ');
    int body = resp.indexOf("\r\n\r\n");
    Serial.printf("[自检] %-13s HTTP %s  %u 字节\n", t.path,
                  sp > 0 ? resp.substring(sp + 1, resp.indexOf('\r', sp)).c_str() : "??",
                  (unsigned)(resp.length() - (body > 0 ? body + 4 : 0)));
  }
}

// ---------------- 主流程 ----------------
void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\n=== ESP32-S3-LCD-2 网页图传 v3 ===");
  Serial.printf("芯片 %s rev%d, PSRAM %s, 空闲堆 %lu KB\n",
                ESP.getChipModel(), ESP.getChipRevision(),
                psramFound() ? "有" : "无", (unsigned long)(ESP.getFreeHeap() / 1024));

  s_cam_mtx = xSemaphoreCreateMutex();

  init_sd();
  if (!init_camera()) Serial.println("[CAM] 摄像头不可用，仅提供状态页");

  start_wifi();
  start_http();

  Serial.printf("\n打开浏览器: http://%s/\n\n", cur_ip().toString().c_str());
  delay(300);
  self_test();
}

void loop() {
  static uint32_t last_hb = 0, last_self = 0;

  if (millis() - last_hb > 5000) {
    last_hb = millis();
    Serial.printf("[%lus] 帧:%lu fps:%.1f 堆:%lu\n", millis() / 1000,
                  (unsigned long)s_frame_count, (double)s_fps,
                  (unsigned long)(ESP.getFreeHeap() / 1024));
  }

  // 每 30 秒自检一次：板子有客户端在用时也能看出服务端是否正常
  if (millis() - last_self > 30000) {
    last_self = millis();
    if (s_server) self_test();
  }

  delay(100);
}
