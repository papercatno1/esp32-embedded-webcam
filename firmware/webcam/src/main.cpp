// ESP32-S3-LCD-2 网页图传（MVP）
// 功能：OV5640 抓帧 → 浏览器 MJPEG 实时预览 + 单帧下载 + 拍照存 TF 卡 + 状态接口
// 板子：微雪 Waveshare ESP32-S3-LCD-2（8MB PSRAM / 16MB Flash / ST7789T3 屏 / OV5640）
// 串口：COM16 @115200（USB-CDC）

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
static volatile uint32_t s_frame_count = 0;   // 累计抓帧数
static volatile float    s_fps = 0;           // 估算帧率
static bool  s_sd_ok = false;
static char  s_res[16] = "--";
static uint32_t s_last_frame_ms = 0;

#define BOUNDARY "frameboundary"

// ---------------- 摄像头 ----------------
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

  // 有 PSRAM 就给大缓冲、双帧；没有就降级
  if (psramFound()) {
    cfg.frame_size   = FRAMESIZE_VGA;      // 640x480，图传够用
    cfg.jpeg_quality = 12;                 // 0-63，越小越清晰
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
    Serial.println("[CAM] 0x20004/0x20005 = 检测不到/不支持的传感器");
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  Serial.printf("[CAM] 传感器 PID=0x%02X VER=0x%02X，图像 %dx%d\n",
                s->id.PID, s->id.VER,
                resolution[s->status.framesize].width,
                resolution[s->status.framesize].height);
  snprintf(s_res, sizeof(s_res), "%dx%d",
           resolution[s->status.framesize].width,
           resolution[s->status.framesize].height);
  return true;
}

// ---------------- TF 卡（可选） ----------------
// 本板 TF 座走 SPI，与 LCD 共用总线：39=CLK 38=MOSI 40=MISO 41=CS
static SPIClass s_sd_spi(HSPI);

static void init_sd() {
  s_sd_spi.begin(39, 40, 38, 41);          // SCK, MISO, MOSI, SS
  s_sd_ok = SD.begin(41, s_sd_spi, 20000000);
  if (!s_sd_ok) {
    Serial.println("[SD] 未检测到卡（不影响图传）");
    return;
  }
  Serial.printf("[SD] 卡已挂载，容量 %.0f MB\n",
                SD.cardSize() / (1024.0 * 1024.0));
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

// ---------------- HTTP 处理 ----------------
static esp_err_t h_index(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

// MJPEG 流：multipart/x-mixed-replace，浏览器 <img> 直接能显示
static esp_err_t h_stream(httpd_req_t *req) {
  static const char *CT  = "multipart/x-mixed-replace;boundary=" BOUNDARY;
  static const char *BND = "\r\n--" BOUNDARY "\r\n";
  static const char *P1  = "Content-Type: image/jpeg\r\nContent-Length: ";
  static const char *P2  = "\r\n\r\n";

  esp_err_t res = httpd_resp_set_type(req, CT);
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "X-Framerate", "60");

  char head[64];
  while (true) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("[CAM] 抓帧失败");
      return ESP_FAIL;
    }

    res = httpd_resp_send_chunk(req, BND, strlen(BND));
    if (res == ESP_OK) {
      int n = snprintf(head, sizeof(head), "%s%u%s", P1, (unsigned)fb->len, P2);
      res = httpd_resp_send_chunk(req, head, n);
    }
    if (res == ESP_OK && fb->format == PIXFORMAT_JPEG) {
      res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    }
    esp_camera_fb_return(fb);
    s_frame_count++;

    // 估算帧率（滑动平均）
    uint32_t now = millis();
    if (s_last_frame_ms) {
      float inst = 1000.0f / (now - s_last_frame_ms + 1);
      s_fps = s_fps * 0.85f + inst * 0.15f;
    }
    s_last_frame_ms = now;

    if (res != ESP_OK) break;   // 浏览器断开
  }
  return res;
}

// 单帧下载
static esp_err_t h_capture(httpd_req_t *req) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return httpd_resp_send_500(req);
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=snap.jpg");
  esp_err_t r = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return r;
}

// 拍照存卡
static esp_err_t h_shoot(httpd_req_t *req) {
  char msg[160];
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    httpd_resp_sendstr(req, "抓帧失败");
    return ESP_OK;
  }
  if (!s_sd_ok) {
    esp_camera_fb_return(fb);
    httpd_resp_sendstr(req, "没有 TF 卡，先插卡再试");
    return ESP_OK;
  }
  char path[64];
  snprintf(path, sizeof(path), "/photo/%lu.jpg", (unsigned long)millis());
  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    esp_camera_fb_return(fb);
    httpd_resp_sendstr(req, "写卡失败");
    return ESP_OK;
  }
  size_t n = f.write(fb->buf, fb->len);
  f.close();
  esp_camera_fb_return(fb);

  snprintf(msg, sizeof(msg), "已保存 %s（%.1f KB）", path, n / 1024.0);
  Serial.println(msg);
  httpd_resp_sendstr(req, msg);
  return ESP_OK;
}

// 状态 JSON
static esp_err_t h_status(httpd_req_t *req) {
  char json[320];
  const char *mode = (WiFi.getMode() == WIFI_AP) ? "AP" : "STA";
  int rssi = (WiFi.getMode() == WIFI_AP) ? 0 : WiFi.RSSI();
  IPAddress ip = (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP() : WiFi.localIP();
  snprintf(json, sizeof(json),
           "{\"mode\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"res\":\"%s\","
           "\"fps\":%.1f,\"frames\":%lu,\"heap\":%lu,\"uptime\":%lu,\"sd\":%s}",
           mode, ip.toString().c_str(), rssi, s_res,
           (double)s_fps, (unsigned long)s_frame_count,
           (unsigned long)ESP.getFreeHeap(), (unsigned long)millis(),
           s_sd_ok ? "true" : "false");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_restart(httpd_req_t *req) {
  httpd_resp_sendstr(req, "restarting");
  delay(200);
  ESP.restart();
  return ESP_OK;
}

static void start_http() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port      = 80;
  cfg.ctrl_port        = 32768;
  cfg.max_uri_handlers = 8;
  cfg.lru_purge_enable = true;
  cfg.stack_size       = 8192;

  if (httpd_start(&s_server, &cfg) != ESP_OK) {
    Serial.println("[HTTP] 服务启动失败");
    return;
  }
  httpd_uri_t uris[] = {
      {"/",        HTTP_GET,  h_index,   nullptr},
      {"/stream",  HTTP_GET,  h_stream,  nullptr},
      {"/capture", HTTP_GET,  h_capture, nullptr},
      {"/shoot",   HTTP_GET,  h_shoot,   nullptr},
      {"/status",  HTTP_GET,  h_status,  nullptr},
      {"/restart", HTTP_GET,  h_restart, nullptr},
  };
  for (auto &u : uris) httpd_register_uri_handler(s_server, &u);
  Serial.println("[HTTP] 服务已启动，端口 80");
}

// ---------------- 主流程 ----------------
void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\n=== ESP32-S3-LCD-2 网页图传 ===");
  Serial.printf("芯片: %s rev%d, PSRAM: %s (%u KB)\n",
                ESP.getChipModel(), ESP.getChipRevision(),
                psramFound() ? "有" : "无", ESP.getPsramSize() / 1024);

  init_sd();
  if (!init_camera()) {
    Serial.println("[CAM] 摄像头不可用，仅提供状态页（检查 24pin FPC）");
  }
  start_wifi();
  start_http();

  IPAddress ip = (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP() : WiFi.localIP();
  Serial.printf("\n打开浏览器: http://%s/\n\n", ip.toString().c_str());
}

void loop() {
  // 每 5 秒打一次心跳，方便串口确认还活着
  static uint32_t last = 0;
  if (millis() - last > 5000) {
    last = millis();
    Serial.printf("[%lus] 帧:%lu fps:%.1f 堆:%lu\n",
                  millis() / 1000, (unsigned long)s_frame_count,
                  (double)s_fps, (unsigned long)ESP.getFreeHeap());
  }
  delay(100);
}
