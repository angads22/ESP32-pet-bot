/*
 *  PetBot — Phase 1.1 single-file Arduino sketch
 *  ----------------------------------------------
 *  Board     : Freenove ESP32-S3 WROOM CAM
 *  What it does:
 *    1. Boots an open WPA2 WiFi AP   (SSID: PetBot_xxxx, password: petbot123)
 *    2. Serves a web app at          http://192.168.4.1/
 *    3. Live MJPEG camera stream at  http://192.168.4.1/stream
 *    4. Accepts                      http://192.168.4.1/cmd?c=MOVE:fwd
 *       (prints commands to Serial for now — servo wiring is Step 1.2)
 *
 *  ── Arduino IDE 2.x setup ──────────────────────────────────────────────
 *  1. File → Preferences → Additional boards manager URLs, add:
 *       https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
 *  2. Tools → Board → Boards Manager → search "esp32" → install
 *     "esp32 by Espressif Systems" version ≥ 2.0.14.
 *  3. Tools → Board → ESP32 Arduino → "ESP32S3 Dev Module"
 *  4. Tools settings:
 *       USB CDC On Boot        : Enabled
 *       CPU Frequency          : 240MHz (WiFi)
 *       Flash Mode             : QIO 80MHz
 *       Flash Size             : 8MB (64Mb)        ← or 4MB if your board has 4MB
 *       Partition Scheme       : Huge APP (3MB No OTA/1MB SPIFFS)
 *       PSRAM                  : OPI PSRAM
 *       Upload Speed           : 921600
 *  5. Plug in via USB-C (a DATA cable, not charge-only).
 *  6. Tools → Port → pick the new /dev/cu.usbmodem* or COMx
 *  7. Sketch → Upload.
 *
 *  If upload fails: hold BOOT, tap RESET, release BOOT, click Upload.
 *  After flashing it auto-resets. Open Tools → Serial Monitor at 115200.
 *
 *  ── Expected serial output ─────────────────────────────────────────────
 *    === PetBot Phase 1.1 booting ===
 *    [cam] OV2640 ready
 *    [wifi] AP: PetBot_xxxx  pw: petbot123  IP: 192.168.4.1
 *    [http] up
 *    === PetBot ready ===
 *
 *  This sketch is the standalone equivalent of the "petbot_s3_ap" env in
 *  the PlatformIO build. The PlatformIO build is the canonical version
 *  (modular C++, BLE NUS, gait engine to come). Use whichever you prefer.
 */

#include <WiFi.h>
#include "esp_http_server.h"
#include "esp_camera.h"

// ─── Freenove ESP32-S3 WROOM CAM camera pin map ─────────────────────────
#define CAM_PWDN   -1
#define CAM_RESET  -1
#define CAM_XCLK   15
#define CAM_SIOD    4
#define CAM_SIOC    5
#define CAM_D7     16
#define CAM_D6     17
#define CAM_D5     18
#define CAM_D4     12
#define CAM_D3     10
#define CAM_D2      8
#define CAM_D1      9
#define CAM_D0     11
#define CAM_VSYNC   6
#define CAM_HREF    7
#define CAM_PCLK   13

// ─── WiFi AP identity ───────────────────────────────────────────────────
#define AP_PREFIX  "PetBot_"
#define AP_PASS    "petbot123"

static httpd_handle_t s_httpd = nullptr;
static char           s_ssid[32] = {0};

// ─── Web app (single-page, served at "/") ───────────────────────────────
static const char WEBAPP_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>PetBot</title>
<style>
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{font-family:system-ui,sans-serif;background:#0a0a14;color:#fff;margin:0 auto;padding:12px;max-width:520px}
h1{text-align:center;margin:8px 0;color:#e94560;letter-spacing:.04em}
.stream{width:100%;border-radius:8px;background:#16213e;display:block;aspect-ratio:4/3}
#st{padding:8px;background:#16213e;border-radius:8px;margin:12px 0;font:12px ui-monospace,monospace;word-break:break-all}
h3{margin:16px 0 6px;color:#e94560;font-size:13px;text-transform:uppercase;letter-spacing:.06em}
.pad{display:grid;grid-template-columns:repeat(3,1fr);gap:6px}
button{background:#16213e;color:#fff;border:2px solid #e94560;border-radius:8px;padding:14px;font-size:18px;cursor:pointer;font-family:inherit}
button:active{background:#e94560}
</style></head><body>
<h1>PetBot</h1>
<img class="stream" src="/stream" alt="camera feed">
<div id="st">connecting…</div>

<h3>Move</h3>
<div class="pad">
  <i></i>
  <button data-cmd="MOVE:fwd">&#9650;</button>
  <i></i>
  <button data-cmd="MOVE:left">&#9664;</button>
  <button onclick="c('MOVE:stop')">&#9632;</button>
  <button data-cmd="MOVE:right">&#9654;</button>
  <i></i>
  <button data-cmd="MOVE:back">&#9660;</button>
  <i></i>
</div>

<h3>Face</h3>
<div class="pad">
  <button onclick="c('FACE:IDLE')">idle</button>
  <button onclick="c('FACE:HAPPY')">happy</button>
  <button onclick="c('FACE:SLEEP')">sleep</button>
</div>

<script>
const $=id=>document.getElementById(id);
function c(cmd){fetch('/cmd?c='+encodeURIComponent(cmd)).then(r=>r.text()).then(t=>$('st').textContent=t).catch(()=>{$('st').textContent='offline'})}
document.querySelectorAll('button[data-cmd]').forEach(btn=>{
  const cmd=btn.dataset.cmd;
  const press=e=>{e.preventDefault();c(cmd)};
  const release=e=>{e.preventDefault();c('MOVE:stop')};
  btn.addEventListener('mousedown',press);
  btn.addEventListener('touchstart',press,{passive:false});
  btn.addEventListener('mouseup',release);
  btn.addEventListener('mouseleave',release);
  btn.addEventListener('touchend',release);
});
setInterval(()=>{fetch('/cmd?c=STATUS').then(r=>r.text()).then(t=>$('st').textContent=t).catch(()=>{})},2000);
</script>
</body></html>)HTML";

// ─── HTTP handlers ──────────────────────────────────────────────────────
static esp_err_t handle_root(httpd_req_t* r) {
    httpd_resp_set_type(r, "text/html");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_sendstr(r, WEBAPP_HTML);
    return ESP_OK;
}

static void url_decode(char* dst, const char* src, size_t dst_sz) {
    size_t di = 0;
    for (size_t i = 0; src[i] && di + 1 < dst_sz; i++) {
        if (src[i] == '%' && isxdigit((uint8_t)src[i+1]) && isxdigit((uint8_t)src[i+2])) {
            char hex[3] = {src[i+1], src[i+2], 0};
            dst[di++] = (char)strtol(hex, nullptr, 16);
            i += 2;
        } else {
            dst[di++] = (src[i] == '+') ? ' ' : src[i];
        }
    }
    dst[di] = '\0';
}

static esp_err_t handle_cmd(httpd_req_t* r) {
    char q[80] = {};
    char reply[128] = {};
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK) {
        char raw[64] = {}, dec[64] = {};
        if (httpd_query_key_value(q, "c", raw, sizeof(raw)) == ESP_OK) {
            url_decode(dec, raw, sizeof(dec));
            Serial.print("[cmd] "); Serial.println(dec);
            if (strcmp(dec, "STATUS") == 0) {
                snprintf(reply, sizeof(reply),
                         "STATUS:ok ssid=%s up=%lus", s_ssid, millis() / 1000);
            } else {
                snprintf(reply, sizeof(reply), "OK:%s", dec);
            }
        }
    }
    httpd_resp_sendstr(r, reply[0] ? reply : "ok");
    return ESP_OK;
}

static esp_err_t handle_stream(httpd_req_t* req) {
    camera_fb_t* fb = nullptr;
    esp_err_t    res = ESP_OK;
    char         hdr[64];
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) { res = ESP_FAIL; break; }
        size_t n = snprintf(hdr, sizeof(hdr),
            "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
            (unsigned)fb->len);
        res = httpd_resp_send_chunk(req, hdr, n);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, "\r\n", 2);
        esp_camera_fb_return(fb);
        if (res != ESP_OK) break;
    }
    return res;
}

// ─── Bring-up ───────────────────────────────────────────────────────────
static bool init_camera() {
    camera_config_t cfg = {};
    cfg.ledc_channel = LEDC_CHANNEL_0;
    cfg.ledc_timer   = LEDC_TIMER_0;
    cfg.pin_d0 = CAM_D0; cfg.pin_d1 = CAM_D1; cfg.pin_d2 = CAM_D2;
    cfg.pin_d3 = CAM_D3; cfg.pin_d4 = CAM_D4; cfg.pin_d5 = CAM_D5;
    cfg.pin_d6 = CAM_D6; cfg.pin_d7 = CAM_D7;
    cfg.pin_xclk = CAM_XCLK; cfg.pin_pclk = CAM_PCLK;
    cfg.pin_vsync = CAM_VSYNC; cfg.pin_href = CAM_HREF;
    cfg.pin_sscb_sda = CAM_SIOD; cfg.pin_sscb_scl = CAM_SIOC;
    cfg.pin_pwdn = CAM_PWDN;  cfg.pin_reset = CAM_RESET;
    cfg.xclk_freq_hz = 20000000;
    cfg.pixel_format = PIXFORMAT_JPEG;
    cfg.frame_size   = FRAMESIZE_QVGA;   // 320x240 — bump after the link is stable
    cfg.jpeg_quality = 10;               // 0=best, 63=worst
    cfg.fb_count     = 2;
    if (esp_camera_init(&cfg) != ESP_OK) {
        Serial.println("[cam] init FAILED");
        return false;
    }
    Serial.println("[cam] OV2640 ready");
    return true;
}

static bool init_wifi_ap() {
    uint64_t mac = ESP.getEfuseMac();
    snprintf(s_ssid, sizeof(s_ssid), AP_PREFIX "%04x", (unsigned)(mac & 0xFFFF));
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(s_ssid, AP_PASS)) {
        Serial.println("[wifi] softAP FAILED");
        return false;
    }
    Serial.printf("[wifi] AP: %s  pw: %s  IP: %s\n",
                  s_ssid, AP_PASS, WiFi.softAPIP().toString().c_str());
    return true;
}

static bool init_http() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 8;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        Serial.println("[http] httpd_start FAILED");
        return false;
    }
    httpd_uri_t r1 = { "/",       HTTP_GET, handle_root,   nullptr };
    httpd_uri_t r2 = { "/cmd",    HTTP_GET, handle_cmd,    nullptr };
    httpd_uri_t r3 = { "/stream", HTTP_GET, handle_stream, nullptr };
    httpd_register_uri_handler(s_httpd, &r1);
    httpd_register_uri_handler(s_httpd, &r2);
    httpd_register_uri_handler(s_httpd, &r3);
    Serial.println("[http] up");
    return true;
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=== PetBot Phase 1.1 booting ===");
    init_camera();
    init_wifi_ap();
    init_http();
    Serial.println("=== PetBot ready ===");
}

void loop() {
    // Nothing here yet — HTTP runs in its own task. Servos / gait land in Step 1.2.
    delay(100);
}
