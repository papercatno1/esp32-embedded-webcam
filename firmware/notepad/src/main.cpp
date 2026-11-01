// ESP32-S3-LCD-2 票据屏 M1
//
// 手机通过 BLE(NUS) 推送条目，设备存 FFat 并显示在 320x240 屏幕上。
// 同时保留 WiFi + HTTP OTA（http://esp32note.local/ota），方便无线升级。
//
// BLE 服务：Nordic UART Service
//   RX 6E400002-B5A3-F393-E0A9-E50E24DCCA9E  write      手机 -> 设备
//   TX 6E400003-B5A3-F393-E0A9-E50E24DCCA9E  notify     设备 -> 手机
// 协议：一行一条指令，以 '\n' 结尾，UTF-8
//   V                      -> OK V <ver>
//   T <标题>|<行1>|<行2>…  -> OK T <id> <总数>
//   L                      -> OK L <总数>  随后每条 ITEM <id> <标题>
//   P <id>                 -> OK P <id>
//   N / R                  -> 下一条 / 上一条
//   D <id>                 -> OK D <id>
//   C                      -> OK C
//   B <0-100>              -> OK B <pct>
//   S <0|1>                -> OK S <0|1>        手动熄屏/点亮
//   O <秒>                 -> OK O <秒>         自动熄屏超时（0=常亮，默认 60）
//   A                      -> OK A <x> <y> <z>  读加速度（g，调试用）
//   其它                   -> ERR UNKNOWN
//
// M2：一段时间无交互自动熄屏（背光关），敲一下板子（QMI8658 加速度突变）点亮。

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_http_server.h>
#include <SD.h>
#include <Update.h>
#include <NimBLEDevice.h>

#include "wifi_config.h"
#include "cjk.h"
#include "notes.h"
#include "ui.h"
#include "qmi8658.h"
#include "power.h"

#define FW_VERSION "0.3.4"

// ---------------- BLE ----------------
#define NUS_SVC "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX  "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_TX  "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

static NimBLECharacteristic *s_tx = nullptr;
static volatile bool s_ble_conn = false;
static volatile int  s_ble_mtu = 23;
static volatile uint32_t s_cmd_rx = 0;   // 收到的指令条数（诊断用）
static char s_last_cmd[56] = "-";        // 最后一条指令（串口抓不到时看这个）
static char s_last_rep[56] = "-";        // 最后一条回执

struct CmdLine { char text[1600]; };
static QueueHandle_t s_cmdq = nullptr;
static char s_rxbuf[1600];
static size_t s_rxlen = 0;

// ---- 图片接收状态（I/A/E 指令）----
#define IMG_BUF_MAX (16 * 1024)     // 320x212 二值图最坏 8.5 KB，RLE 后远小于此
static uint8_t *s_img_buf = nullptr;
static size_t   s_img_len = 0;
static int      s_img_w = 0, s_img_h = 0;

// base64 解码（忽略换行与 '='）；返回写入字节数
static size_t b64_decode(const char *src, uint8_t *dst, size_t cap) {
  static int8_t tbl[256];
  static bool init = false;
  if (!init) {
    memset(tbl, -1, sizeof(tbl));
    const char *a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; i++) tbl[(uint8_t)a[i]] = (int8_t)i;
    init = true;
  }
  size_t n = 0;
  uint32_t acc = 0;
  int bits = 0;
  for (const char *p = src; *p; p++) {
    int8_t v = tbl[(uint8_t)*p];
    if (v < 0) continue;            // '='、空白、非法字符都跳过
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (n >= cap) return n;
      dst[n++] = (uint8_t)((acc >> bits) & 0xFF);
    }
  }
  return n;
}

// ---- TF 卡（和 LCD 共用 SCK39/MOSI38，CS41、MISO40）----
static SPIClass s_sd_spi(HSPI);
static bool     s_sd_ok = false;
static uint64_t s_sd_mb = 0;
static bool     s_sd_hi_done = false;
static bool     s_sd_present = false;
static bool     s_sd_probed  = false;

// 低层探一下：只发 CMD0，看卡在不在（区分"没卡"和"卡在但格式不认"）
static bool sd_card_present() {
  pinMode(41, OUTPUT);
  digitalWrite(41, HIGH);
  s_sd_spi.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  for (int i = 0; i < 10; i++) s_sd_spi.transfer(0xFF);   // 80 个空时钟唤醒卡
  digitalWrite(41, LOW);
  const uint8_t cmd0[6] = {0x40, 0, 0, 0, 0, 0x95};        // CMD0 + CRC7(0x95)
  for (int i = 0; i < 6; i++) s_sd_spi.transfer(cmd0[i]);
  uint8_t r1 = 0xFF;
  for (int i = 0; i < 8 && (r1 & 0x80); i++) r1 = s_sd_spi.transfer(0xFF);
  digitalWrite(41, HIGH);
  s_sd_spi.transfer(0xFF);
  s_sd_spi.endTransaction();
  return (r1 != 0xFF);
}

static bool sd_probe() {
  if (!s_sd_hi_done) { s_sd_spi.begin(39, 40, 38, 41); s_sd_hi_done = true; }
  s_sd_ok = SD.begin(41, s_sd_spi, 20000000);
  if (!s_sd_ok) s_sd_ok = SD.begin(41, s_sd_spi, 4000000);  // 慢速再试一次（老卡常见）
  if (!s_sd_ok) {
    s_sd_present = sd_card_present();
    s_sd_mb = 0;
    Serial.printf("[SD] 挂载失败（卡%s）\n", s_sd_present ? "在，但不认格式" : "不在");
    return false;
  }
  s_sd_mb = SD.cardSize() / (1024ULL * 1024ULL);
  s_sd_present = true;
  Serial.printf("[SD] 已挂载 %llu MB\n", (unsigned long long)s_sd_mb);
  return true;
}

static void ble_send(const char *s) {
  snprintf(s_last_rep, sizeof(s_last_rep), "%.40s", s);
  if (!s_tx || !s_ble_conn) return;
  s_tx->setValue((uint8_t *)s, strlen(s));
  s_tx->notify();
  Serial.printf("[BLE>] %s\n", s);
}

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c) override {
    std::string v = c->getValue();
    for (unsigned char ch : v) {
      if (ch == '\n' || ch == '\r') {
        if (s_rxlen == 0) continue;
        s_rxbuf[s_rxlen] = 0;
        CmdLine cl;
        strncpy(cl.text, s_rxbuf, sizeof(cl.text) - 1);
        cl.text[sizeof(cl.text) - 1] = 0;
        s_rxlen = 0;
        if (s_cmdq) xQueueSend(s_cmdq, &cl, 0);
      } else if (s_rxlen < sizeof(s_rxbuf) - 1) {
        s_rxbuf[s_rxlen++] = (char)ch;
      }
    }
  }
};

class SrvCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *s) override {
    s_ble_conn = true;
    Serial.println("[BLE] 已连接");
    ui_set_link(true);
    ui_footer_refresh();
  }
  void onMTUChange(uint16_t mtu, ble_gap_conn_desc *desc) override {
    s_ble_mtu = mtu;
    Serial.printf("[BLE] MTU=%u\n", mtu);
  }
  void onDisconnect(NimBLEServer *s) override {
    s_ble_conn = false;
    Serial.println("[BLE] 断开，重新广播");
    ui_set_link(false);
    ui_footer_refresh();
    NimBLEDevice::startAdvertising();
  }
};

static void ble_begin() {
  NimBLEDevice::init("ESP32-NOTE");
  NimBLEDevice::setMTU(517);
  NimBLEServer *srv = NimBLEDevice::createServer();
  srv->setCallbacks(new SrvCallbacks());
  NimBLEService *svc = srv->createService(NUS_SVC);
  NimBLECharacteristic *rx = svc->createCharacteristic(
      NUS_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(new RxCallbacks());
  s_tx = svc->createCharacteristic(NUS_TX, NIMBLE_PROPERTY::NOTIFY);
  svc->start();

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SVC);
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  NimBLEDevice::startAdvertising();
  Serial.println("[BLE] 广播中: ESP32-NOTE");
}

// ---------------- WiFi / OTA ----------------
static httpd_handle_t s_http = nullptr;
static bool s_wifi_ok = false;

static IPAddress cur_ip() {
  return (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP() : WiFi.localIP();
}

static const char OTA_PAGE[] PROGMEM =
  "<!doctype html><meta charset=utf-8><title>OTA</title>"
  "<style>body{font:16px/1.6 sans-serif;margin:24px;max-width:520px}"
  "button{padding:10px 18px;font-size:16px}#p{height:18px;background:#eee}"
  "#b{height:18px;background:#3b82f6;width:0}</style>"
  "<h2>ESP32-NOTE 固件升级</h2>"
  "<p>选 .bin 文件 → 上传 → 自动重启</p>"
  "<input id=f type=file accept=.bin><button onclick=go()>上传升级</button>"
  "<div id=p><div id=b></div></div><p id=s></p>"
  "<script>function go(){var f=document.getElementById('f').files[0];"
  "if(!f){alert('先选文件');return}"
  "var x=new XMLHttpRequest();x.open('POST','/update');"
  "x.upload.onprogress=function(e){var p=e.loaded/e.total*100;"
  "document.getElementById('b').style.width=p+'%';"
  "document.getElementById('s').textContent='上传中 '+p.toFixed(0)+'%'};"
  "x.onload=function(){document.getElementById('s').textContent="
  "x.status==200?'完成，等待重启…':'失败: '+x.responseText};"
  "x.onerror=function(){document.getElementById('s').textContent='网络错误'};"
  "x.setRequestHeader('Content-Type','application/octet-stream');"
  "x.send(f)}</script>";

static esp_err_t h_root(httpd_req_t *req) {
  String s = "<!doctype html><meta charset=utf-8><meta http-equiv=refresh content=5>"
             "<style>body{font:18px/1.8 sans-serif;margin:24px}</style>";
  s += "<h2>ESP32-NOTE 票据屏</h2>";
  s += "<p>固件: " FW_VERSION "</p>";
  s += "<p>IP: " + cur_ip().toString() + "</p>";
  s += "<p>条目: " + String(notes_count()) + " 条</p>";
  s += "<p>BLE: " + String(s_ble_conn ? "已连接" : "广播中") + "</p>";
  s += "<p><a href=/ota>固件升级</a></p>";
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, s.c_str(), s.length());
}

static esp_err_t h_status(httpd_req_t *req) {
  char buf[640];
  snprintf(buf, sizeof(buf),
           "{\"ver\":\"" FW_VERSION "\",\"up\":%lu,\"heap\":%u,\"wifi\":\"%s\","
           "\"ip\":\"%s\",\"rssi\":%d,\"ble\":%d,\"mtu\":%d,\"notes\":%d,\"cur\":%d,"
           "\"bl\":%d,\"ot\":%lu,\"imu\":%d,\"idle\":%lu,\"rx\":%lu,\"sw\":%lu,"
           "\"swd\":%.1f,\"swa\":\"%c\",\"gnw\":%.0f,\"gpk\":%.0f,\"sn\":%d,\"sd\":%d,\"sdm\":%llu,\"ifs\":\"%s\",\"lc\":\"%s\",\"lr\":\"%s\",\"iw\":%d,\"ih\":%d,\"il\":%u,\"isz\":%u,\"bk\":%u,\"fl\":%u}",
           (unsigned long)(millis() / 1000), (unsigned)ESP.getFreeHeap(),
           (WiFi.getMode() == WIFI_AP) ? "ap" : "sta",
           cur_ip().toString().c_str(),
           (WiFi.getMode() == WIFI_AP) ? 0 : WiFi.RSSI(),
           (int)s_ble_conn, s_ble_mtu, notes_count(), notes_cursor(),
           power_is_blank() ? 0 : 1,
           (unsigned long)(power_timeout() / 1000),
           qmi_ok() ? 1 : 0,
           (unsigned long)(power_idle_ms() / 1000),
           (unsigned long)s_cmd_rx,
           (unsigned long)power_swipe_count(),
           power_last_swipe_deg(), power_last_swipe_axis(),
           power_last_mag(), power_peak_mag(), power_get_swipe_sign(),
           s_sd_ok ? 1 : 0, (unsigned long long)s_sd_mb, notes_img_fs_name(),
           s_last_cmd, s_last_rep, s_img_w, s_img_h, (unsigned)s_img_len,
           (unsigned)notes_img_index_size(),
           (unsigned)ui_last_img_ink(), (unsigned)power_flip_count());
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_ota_page(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, OTA_PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_update(httpd_req_t *req) {
  if (req->content_len <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
  Serial.printf("[OTA] 开始 %u 字节\n", (unsigned)req->content_len);
  ui_progress(0, "固件升级中");

  if (!Update.begin(req->content_len, U_FLASH)) {
    ui_message("升级失败", Update.errorString());
    return httpd_resp_send(req, Update.errorString(), HTTPD_RESP_USE_STRLEN);
  }
  uint8_t buf[2048];
  size_t got = 0;
  int last_pct = -1;
  while (got < (size_t)req->content_len) {
    int r = httpd_req_recv(req, (char *)buf, sizeof(buf));
    if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (r <= 0) { Update.abort(); return httpd_resp_send(req, "recv error", HTTPD_RESP_USE_STRLEN); }
    if (Update.write(buf, r) != (size_t)r) {
      Update.abort();
      return httpd_resp_send(req, Update.errorString(), HTTPD_RESP_USE_STRLEN);
    }
    got += r;
    int pct = (int)(got * 100 / req->content_len);
    if (pct / 5 != last_pct / 5) { last_pct = pct; ui_progress(pct, "固件升级中"); }
  }
  if (!Update.end(true)) {
    ui_message("校验失败", Update.errorString());
    return httpd_resp_send(req, Update.errorString(), HTTPD_RESP_USE_STRLEN);
  }
  ui_progress(100, "升级完成，重启");
  httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
  delay(800);
  ESP.restart();
  return ESP_OK;
}

static void http_begin() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.max_uri_handlers = 8;
  cfg.lru_purge_enable = true;
  if (httpd_start(&s_http, &cfg) != ESP_OK) {
    Serial.println("[HTTP] 启动失败");
    return;
  }
  const httpd_uri_t uris[] = {
      {"/",            HTTP_GET,  h_root,      nullptr},
      {"/status.json", HTTP_GET,  h_status,    nullptr},
      {"/ota",         HTTP_GET,  h_ota_page,  nullptr},
      {"/update",      HTTP_POST, h_update,    nullptr},
  };
  for (auto &u : uris) httpd_register_uri_handler(s_http, &u);
  Serial.println("[HTTP] http://" + cur_ip().toString() + "/");
}

static void wifi_begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("esp32note");
  if (strlen(WIFI_SSID) == 0) {
    Serial.println("[WiFi] 无凭据，跳过 STA");
    return;
  }
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  ui_message("连接 Wi-Fi…", WIFI_SSID);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(200);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    s_wifi_ok = true;
    Serial.printf("[WiFi] 已连接 %s\n", WiFi.localIP().toString().c_str());
    if (MDNS.begin(MDNS_NAME)) MDNS.addService("http", "tcp", 80);
    http_begin();
  } else {
    Serial.println("[WiFi] 连接失败，仅 BLE 模式");
    WiFi.mode(WIFI_OFF);
  }
}

// ---------------- 指令处理 ----------------
// 图片重绘必须在 loop() 里做：BLE 回调里整屏画图会饿死 NimBLE 主机任务导致重启
static volatile bool s_need_show = false;

static void do_show(int idx) {
  notes_set_cursor(idx);          // 内部按条数取模
  if (notes_count() == 0) {
    ui_show_empty();              // 只有一条都没有时才显示「等待推送」
    return;
  }
  ui_show_note(notes_cursor());   // 必须用取模后的游标，否则越界会画成空白页
}

static void handle_cmd(const char *raw) {
  Serial.printf("[CMD] %s\n", raw);
  s_cmd_rx++;
  snprintf(s_last_cmd, sizeof(s_last_cmd), "%.40s", raw);
  power_activity();          // 任何指令都算一次交互（顺手点亮屏幕）
  char buf[1600];
  strncpy(buf, raw, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;

  char *cmd = buf;
  while (*cmd == ' ') cmd++;
  char *arg = strchr(cmd, ' ');
  if (arg) { *arg++ = 0; while (*arg == ' ') arg++; }

  if (!strcmp(cmd, "V")) {
    char o[48];
    snprintf(o, sizeof(o), "OK V " FW_VERSION);
    ble_send(o);
  } else if (!strcmp(cmd, "T")) {
    if (!arg) { ble_send("ERR NOARG"); return; }
    const char *parts[NOTE_LINES + 1];
    int n = 0;
    char *p = arg;
    parts[n++] = p;
    while (n < NOTE_LINES + 1) {
      char *bar = strchr(p, '|');
      if (!bar) break;
      *bar = 0;
      p = bar + 1;
      parts[n++] = p;
    }
    const char *title = parts[0];
    const char *lines[NOTE_LINES];
    for (int i = 1; i < n; i++) lines[i - 1] = parts[i];
    int id = notes_add(title, lines, n - 1);
    do_show(notes_cursor());
    char o[64];
    snprintf(o, sizeof(o), "OK T %d %d", id, notes_count());
    ble_send(o);
  } else if (!strcmp(cmd, "I")) {
    // I <宽> <高>：开始收一张图，后续用 A <base64> 逐段送 RLE，最后 E 结束
    char o[64];
    int w = 0, h = 0;
    if (arg) sscanf(arg, "%d %d", &w, &h);
    if (w <= 0 || h <= 0 || w > IMG_MAX_W || h > IMG_MAX_H) {
      snprintf(o, sizeof(o), "ERR SIZE");
      ble_send(o);
      return;
    }
    if (!s_img_buf) {
      s_img_buf = (uint8_t *)ps_malloc(IMG_BUF_MAX);
      if (!s_img_buf) s_img_buf = (uint8_t *)malloc(IMG_BUF_MAX);
    }
    if (!s_img_buf) {
      ble_send("ERR NOMEM");
      return;
    }
    s_img_w = w;
    s_img_h = h;
    s_img_len = 0;
    ui_progress(0, "接收图片");
    snprintf(o, sizeof(o), "OK I START %dx%d", w, h);
    ble_send(o);
  } else if (!strcmp(cmd, "A")) {
    if (!s_img_w || !s_img_buf) {
      ble_send("ERR NOIMG");
    } else if (!arg) {
      ble_send("ERR NOARG");
    } else {
      char o[48];
      size_t room = IMG_BUF_MAX - s_img_len;
      size_t got = b64_decode(arg, s_img_buf + s_img_len, room);
      if (got == 0 && room < 4) {
        ble_send("ERR FULL");
      } else {
        s_img_len += got;
        int pct = (int)(s_img_len * 100 / (IMG_BUF_MAX / 2));   // 进度按半个缓冲区封顶，纯粹给个反馈
        if (pct > 99) pct = 99;
        ui_progress(pct, "接收图片");
        snprintf(o, sizeof(o), "OK A %u", (unsigned)s_img_len);
        ble_send(o);
      }
    }
  } else if (!strcmp(cmd, "E")) {
    if (!s_img_w || !s_img_len) {
      ble_send("ERR NOIMG");
    } else {
      char o[64];
      int id = notes_add_img_rle(nullptr, (uint16_t)s_img_w, (uint16_t)s_img_h, s_img_buf, s_img_len, false);
      if (id < 0) {
        ble_send("ERR DECODE");
      } else {
        Note *it = notes_find(id);
        uint32_t hv = (it && it->img) ? notes_img_hash(it->img, (size_t)((it->iw + 7) / 8) * it->ih) : 0;
        snprintf(o, sizeof(o), "OK E %d %u %08X", id, (unsigned)s_img_len, (unsigned)hv);
        ble_send(o);             // ①先回执（此刻还没碰 flash，和 A 一样在 BLE 活跃期）
        notes_save();            // ②再落盘（写 flash 期间 BLE 会卡 1 秒以上）
        s_need_show = true;   // ③整屏重绘放到 loop()（BLE 回调里画会卡死协议栈）
      }
      s_img_w = s_img_h = 0;
      s_img_len = 0;   // 缓冲区留着复用（下次 I 直接覆盖）
    }
  } else if (!strcmp(cmd, "Z")) {
    s_img_w = s_img_h = 0;
    s_img_len = 0;
    do_show(notes_cursor());
    ble_send("OK Z");
  } else if (!strcmp(cmd, "L")) {
    char o[32];
    snprintf(o, sizeof(o), "OK L %d", notes_count());
    ble_send(o);
    for (int i = 0; i < notes_count(); i++) {
      Note *it = notes_get(i);
      char line[NOTE_TITLE_BYTES + 24];
      snprintf(line, sizeof(line), "ITEM %d %s", it->id, it->title);
      ble_send(line);
    }
  } else if (!strcmp(cmd, "P")) {
    int v = arg ? atoi(arg) : 0;
    Note *it = notes_find(v);
    if (it) do_show((int)(it - notes_get(0)));
    else do_show(v - 1);
    char o[32];
    snprintf(o, sizeof(o), "OK P %d", notes_cursor() + 1);
    ble_send(o);
  } else if (!strcmp(cmd, "N")) {
    do_show(notes_cursor() + 1);
    char o[32];
    snprintf(o, sizeof(o), "OK P %d", notes_cursor() + 1);
    ble_send(o);
  } else if (!strcmp(cmd, "R")) {
    do_show(notes_cursor() - 1);
    char o[32];
    snprintf(o, sizeof(o), "OK P %d", notes_cursor() + 1);
    ble_send(o);
  } else if (!strcmp(cmd, "D")) {
    int id = arg ? atoi(arg) : 0;
    bool ok = notes_del(id);
    do_show(notes_cursor());
    ble_send(ok ? "OK D" : "ERR NOTFOUND");
  } else if (!strcmp(cmd, "C")) {
    notes_clear();
    ui_show_empty();
    ble_send("OK C");
  } else if (!strcmp(cmd, "B")) {
    int pct = arg ? atoi(arg) : 100;
    ui_backlight(pct);
    char o[32];
    snprintf(o, sizeof(o), "OK B %d", ui_backlight_get());
    ble_send(o);
  } else if (!strcmp(cmd, "S")) {
    int on = arg ? atoi(arg) : 1;
    if (on) power_wake(); else power_blank();
    ble_send(on ? "OK S 1" : "OK S 0");
  } else if (!strcmp(cmd, "O")) {
    uint32_t sec = arg ? (uint32_t)atoi(arg) : 60;
    if (sec > 86400) sec = 86400;
    if (sec == 0) power_activity();
    power_set_timeout(sec * 1000UL);
    ui_set_timeout_s(power_timeout() / 1000);
    ui_footer_refresh();
    char o[32];
    snprintf(o, sizeof(o), "OK O %lu", (unsigned long)(power_timeout() / 1000));
    ble_send(o);
  } else if (!strcmp(cmd, "A")) {
    float ax = 0, ay = 0, az = 0;
    char o[64];
    if (qmi_read_accel(ax, ay, az)) snprintf(o, sizeof(o), "OK A %.2f %.2f %.2f", ax, ay, az);
    else snprintf(o, sizeof(o), "ERR IMU");
    ble_send(o);
  } else if (!strcmp(cmd, "G")) {
    float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
    char o[96];
    if (qmi_read_motion(ax, ay, az, gx, gy, gz))
      snprintf(o, sizeof(o), "OK G %.2f %.2f %.2f | %.0f %.0f %.0f", ax, ay, az, gx, gy, gz);
    else snprintf(o, sizeof(o), "ERR IMU");
    ble_send(o);
  } else if (!strcmp(cmd, "Q")) {
    // 甩动统计：次数 / 最近角度 / 轴 + 实时与峰值角速度（判断传感器看不看得见你甩）
    char o[128];
    snprintf(o, sizeof(o), "OK Q %lu %.1f %c | now %.0f peak %.0f | %d/%d | k%d",
             (unsigned long)power_swipe_count(),
             power_last_swipe_deg(), power_last_swipe_axis(),
             power_last_mag(), power_peak_mag(),
             notes_count() > 0 ? notes_cursor() + 1 : 0, notes_count(),
             power_get_swipe_sign());
    ble_send(o);
  } else if (!strcmp(cmd, "M")) {
    // 看 TF 卡：第一次会现场挂载（探测会扰动 SPI，之后重画一帧自愈）
    char o[96];
    bool ok = s_sd_ok || sd_probe();
    if (!ok) {
      snprintf(o, sizeof(o), s_sd_present ? "ERR NOFAT" : "ERR NOCARD");
    } else {
      int t = SD.cardType();
      const char *tn = (t == CARD_MMC) ? "MMC" : (t == CARD_SD) ? "SD"
                     : (t == CARD_SDHC) ? "SDHC" : "?";
      // 顺便测一下能不能写：有些出厂 exFAT 卡在 FatFs 下挂得上但写不进去
      bool wr = false;
      File tf = SD.open("/_t.txt", "w");
      if (tf) { tf.print("x"); tf.close(); wr = SD.exists("/_t.txt"); SD.remove("/_t.txt"); }
      snprintf(o, sizeof(o), "OK M %s %llu w=%d", tn, (unsigned long long)s_sd_mb, wr ? 1 : 0);
      notes_refresh_img_fs();
    }
    s_sd_probed = true;
    ui_bus_recover();
    ble_send(o);
    if (notes_count() > 0) ui_show_note(notes_cursor()); else ui_show_empty();
  } else if (!strcmp(cmd, "K")) {
    // 翻页方向：K 1 / K -1；K t = 翻转；不带参数就查询
    if (arg) {
      if (arg[0] == 't' || arg[0] == 'T') power_set_swipe_sign(-power_get_swipe_sign());
      else power_set_swipe_sign(atoi(arg));
    }
    char o[32];
    snprintf(o, sizeof(o), "OK K %d", power_get_swipe_sign());
    ble_send(o);
  } else if (cmd[0]) {
    ble_send("ERR UNKNOWN");
  }
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== ESP32-NOTE 票据屏 " FW_VERSION " ===");

  // TF 卡必须在 LCD 之前挂：SD 与 LCD 共用 SCK(39)/MOSI(38)，
  // 谁后初始化谁把总线拿走 —— LCD 先初始化就会被 SD 弄成花屏。
  ui_begin();
  cjk_init();
  boolean ok = notes_begin();
  Serial.printf("[NOTE] 存储 %s\n", ok ? "可用" : "不可用");

  qmi_begin();          // 板上 QMI8658：敲击唤醒 + 甩动翻页
  power_begin(60000);   // 60 秒无交互熄屏；0 = 常亮
  ui_set_timeout_s(power_timeout() / 1000);
  ui_set_link(false);

  if (notes_count() > 0) ui_show_note(notes_cursor());
  else ui_show_empty();

  s_cmdq = xQueueCreate(6, sizeof(CmdLine));
  ble_begin();
  wifi_begin();

  if (ui_ready()) {
    if (notes_count() > 0) ui_show_note(notes_cursor());
    else ui_show_empty();
  }
  Serial.printf("[SYS] heap=%u\n", (unsigned)ESP.getFreeHeap());
}

void loop() {
  CmdLine cl;
  while (s_cmdq && xQueueReceive(s_cmdq, &cl, 0) == pdTRUE) {
    handle_cmd(cl.text);
  }

  // 串口也能下指令（调试用：BLE 被手机占着时照样能发 K/Q/N 等）
  static char s_ser[128];
  static size_t s_ser_n = 0;
  while (Serial.available()) {
    int c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      s_ser[s_ser_n] = 0;
      if (s_ser_n) handle_cmd(s_ser);
      s_ser_n = 0;
    } else if (s_ser_n < sizeof(s_ser) - 1) {
      s_ser[s_ser_n++] = (char)c;
    }
  }

  power_tick();   // 超时熄屏 / 敲击唤醒 / 甩动检测

  // 甩一下翻页：往右下一条，往左上一条
  if (s_need_show) {
    s_need_show = false;
    do_show(notes_cursor());
  }
  int sw = power_take_swipe();
  if (sw != 0 && notes_count() > 1) {
    do_show(notes_cursor() + (sw > 0 ? 1 : -1));
    power_activity();
  }

  // 广播看门狗：客户端非正常断开时 NimBLE 有时不会自己恢复广播
  // （isAdvertising() 还会骗人说"在广播"，所以每 5 秒无脑重开一次，重开是无害的）
  static uint32_t last_adv = 0;
  if (!s_ble_conn && millis() - last_adv > 5000) {
    last_adv = millis();
    NimBLEDevice::startAdvertising();
  }

  static uint32_t last = 0;
  if (millis() - last > 30000) {
    last = millis();
    Serial.printf("[SYS] up=%lus heap=%u ble=%d notes=%d bl=%d imu=%d idle=%lus\n",
                  (unsigned long)(millis() / 1000), (unsigned)ESP.getFreeHeap(),
                  (int)s_ble_conn, notes_count(), power_is_blank() ? 0 : 1,
                  qmi_ok() ? 1 : 0, (unsigned long)(power_idle_ms() / 1000));
  }
  delay(10);
}
