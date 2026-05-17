// Target: ESP32-S3-CAM (brain)
// Phase 1 of the own-it overhaul: single-board build that boots straight
// into an open WiFi AP and serves a self-contained web app + live MJPEG
// at http://192.168.4.1/. BLE NUS stays available for direct control.
// Captive-portal / WiFi-station (WiFiManager) is retained behind
// PETBOT_ENABLE_WIFI=1 for the later "join home WiFi" flow.

#include "ble_web.h"

#include "app_state.h"
#include "menu_controller.h"

// ─── Build flags ─────────────────────────────────────────────────────────────
#ifndef PETBOT_AP_MODE
#define PETBOT_AP_MODE 0
#endif
#ifndef PETBOT_ENABLE_STREAM
#define PETBOT_ENABLE_STREAM 0
#endif
#ifndef PETBOT_ENABLE_WIFI
#define PETBOT_ENABLE_WIFI 0
#endif

// Derived: HTTP server lives whenever EITHER WiFi mode is on.
#if PETBOT_AP_MODE || PETBOT_ENABLE_WIFI
  #define PETBOT_HTTP_ENABLED 1
#else
  #define PETBOT_HTTP_ENABLED 0
#endif

#if PETBOT_HTTP_ENABLED
  #include <WiFi.h>
  #include "esp_http_server.h"
#endif
#if PETBOT_ENABLE_WIFI
  #include <WiFiManager.h>
  #include <ESPmDNS.h>
#endif
#if PETBOT_ENABLE_STREAM || PETBOT_AP_MODE
  #include "esp_camera.h"
#endif

#if defined(__has_include) && __has_include(<NimBLEDevice.h>)
  #include <NimBLEDevice.h>
  #define PETBOT_USE_NIMBLE 1
#else
  #include <BLEDevice.h>
  #include <BLEServer.h>
  #include <BLEUtils.h>
  #include <BLE2902.h>
  #define PETBOT_USE_NIMBLE 0
#endif

// ─── Identity / UUIDs ────────────────────────────────────────────────────────
#define BLE_DEVICE_NAME   "PetBot"
#define AP_SSID_PREFIX    "PetBot_"           // suffixed with last-4-hex of MAC
#define AP_PASSWORD       "petbot123"         // open networks confuse iOS — use WPA2
#define WIFI_SETUP_SSID   "PETBOT_SETUP"
#define WIFI_SETUP_PASS   "petbot123"
#define MDNS_NAME         "petbot"
#define NUS_SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX_UUID       "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_TX_UUID       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// ─── BLE type aliases (NimBLE vs classic) ────────────────────────────────────
#if PETBOT_USE_NIMBLE
  using PetBleServer         = NimBLEServer;
  using PetBleService        = NimBLEService;
  using PetBleCharacteristic = NimBLECharacteristic;
  using PetBleAdvertising    = NimBLEAdvertising;
#else
  using PetBleServer         = BLEServer;
  using PetBleService        = BLEService;
  using PetBleCharacteristic = BLECharacteristic;
  using PetBleAdvertising    = BLEAdvertising;
#endif

namespace ble_web {

static PetBleServer*         s_ble_server = nullptr;
static PetBleCharacteristic* s_ble_notify = nullptr;
static bool                  s_ble_connected = false;

void send(const String& msg) {
    if (!s_ble_connected || !s_ble_notify) return;
    s_ble_notify->setValue(msg.c_str());
    s_ble_notify->notify();
}

// ─── Command dispatcher (BLE RX + HTTP /cmd) ─────────────────────────────────
static FaceMode parseFaceMode(const String& s, bool& ok) {
    ok = true;
    if      (s == "IDLE")    return FaceMode::IDLE;
    else if (s == "HAPPY")   return FaceMode::HAPPY;
    else if (s == "SEARCH")  return FaceMode::SEARCH;
    else if (s == "CURIOUS") return FaceMode::CURIOUS;
    else if (s == "DRIVE")   return FaceMode::DRIVE;
    else if (s == "SLEEP")   return FaceMode::SLEEP;
    ok = false;
    return FaceMode::IDLE;
}

void handleCommand(const String& cmd) {
    Serial.print("[cmd] "); Serial.println(cmd);
    if (cmd.startsWith("MOVE:")) {
        String dir = cmd.substring(5);
        if      (dir == "fwd")   app_state::moveForward();
        else if (dir == "back")  app_state::moveBack();
        else if (dir == "left")  app_state::moveLeft();
        else if (dir == "right") app_state::moveRight();
        else                     app_state::moveStop();
        send("OK:MOVE:" + dir);
    } else if (cmd.startsWith("FACE:")) {
        bool ok;
        FaceMode m = parseFaceMode(cmd.substring(5), ok);
        if (!ok) { send("ERR:bad_face"); return; }
        app_state::setExpression(m);
        send(String("OK:FACE:") + app_state::expressionName(m));
    } else if (cmd.startsWith("MODE:")) {
        String m = cmd.substring(5);
        if (m == "manual")      app_state::setMode(CtrlMode::MANUAL);
        else if (m == "auto")   app_state::setMode(CtrlMode::AUTO);
        else                  { send("ERR:bad_mode"); return; }
        send(String("OK:MODE:") + app_state::modeName(app_state::mode()));
    } else if (cmd.startsWith("SAY:")) {
        app_state::say(cmd.substring(4).c_str());
        send("OK:SAY");
    } else if (cmd.startsWith("SOUND:")) {
        app_state::playSound(cmd.substring(6).c_str());
        send("OK:SOUND");
    } else if (cmd.startsWith("SCREEN:")) {
        menu_controller::pushDebugLine(cmd.substring(7).c_str());
        send("OK:SCREEN");
    } else if (cmd == "STATUS") {
        String s = String("STATUS:ok face=") + app_state::expressionName(app_state::expression())
                 + " mode=" + app_state::modeName(app_state::mode())
                 + " up=" + String((unsigned long)(millis() / 1000)) + "s";
      #if PETBOT_AP_MODE
        s += " ap=" AP_SSID_PREFIX;
      #endif
      #if PETBOT_ENABLE_WIFI
        s += " web=http://" MDNS_NAME ".local";
      #endif
        send(s);
    } else {
        send("ERR:unknown:" + cmd);
    }
}

// ─── HTTP server + web app ───────────────────────────────────────────────────
#if PETBOT_HTTP_ENABLED

// Single-page app served at "/". Live MJPEG at top, status, hold-to-move
// D-pad, face presets, and a link to the (Step 1.2) calibration screen.
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
.link{display:block;text-align:center;padding:12px;background:#16213e;color:#fff;border:2px solid #e94560;border-radius:8px;text-decoration:none;margin-top:8px}
.link:active{background:#e94560}
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

<a class="link" href="/calibrate">Servo calibration &rarr;</a>

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

// Placeholder until Step 1.2 — landing here tells the user where they are.
static const char CALIBRATE_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PetBot — calibrate</title>
<style>body{font-family:system-ui,sans-serif;background:#0a0a14;color:#fff;padding:20px;max-width:480px;margin:0 auto;text-align:center}h1{color:#e94560}a{display:inline-block;color:#fff;border:2px solid #e94560;padding:10px 20px;border-radius:8px;text-decoration:none;margin-top:20px}</style>
</head><body>
<h1>Servo calibration</h1>
<p>Coming in Step 1.2 — sliders per servo, &ldquo;release torque&rdquo; per leg, &ldquo;Save home&rdquo; to NVS.</p>
<a href="/">&larr; back</a>
</body></html>)HTML";

static httpd_handle_t s_httpd = nullptr;

static esp_err_t handle_root(httpd_req_t* r) {
    httpd_resp_set_type(r, "text/html");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_sendstr(r, WEBAPP_HTML);
    return ESP_OK;
}

static esp_err_t handle_calibrate(httpd_req_t* r) {
    httpd_resp_set_type(r, "text/html");
    httpd_resp_sendstr(r, CALIBRATE_HTML);
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
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK) {
        char raw[64] = {}, dec[64] = {};
        if (httpd_query_key_value(q, "c", raw, sizeof(raw)) == ESP_OK) {
            url_decode(dec, raw, sizeof(dec));
            handleCommand(String(dec));
        }
    }
    httpd_resp_sendstr(r, "ok");
    return ESP_OK;
}

#if PETBOT_ENABLE_STREAM || PETBOT_AP_MODE
static esp_err_t handle_stream(httpd_req_t* req) {
    camera_fb_t* fb = nullptr; esp_err_t res = ESP_OK; char hdr[64];
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
#endif

static void start_http_server() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 8;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        Serial.println("[HTTP] httpd_start FAILED");
        return;
    }
    httpd_uri_t ru = { "/",          HTTP_GET, handle_root,      nullptr };
    httpd_uri_t cu = { "/cmd",       HTTP_GET, handle_cmd,       nullptr };
    httpd_uri_t kl = { "/calibrate", HTTP_GET, handle_calibrate, nullptr };
    httpd_register_uri_handler(s_httpd, &ru);
    httpd_register_uri_handler(s_httpd, &cu);
    httpd_register_uri_handler(s_httpd, &kl);
#if PETBOT_ENABLE_STREAM || PETBOT_AP_MODE
    httpd_uri_t su = { "/stream",    HTTP_GET, handle_stream,    nullptr };
    httpd_register_uri_handler(s_httpd, &su);
#endif
    Serial.println("[HTTP] Web UI active");
}

#endif  // PETBOT_HTTP_ENABLED

// ─── WiFi bring-up paths ─────────────────────────────────────────────────────
#if PETBOT_AP_MODE
static void setup_wifi() {
    // Pure AP mode: no station, no captive portal, no auto-connect.
    char ssid[32];
    uint64_t mac = ESP.getEfuseMac();
    snprintf(ssid, sizeof(ssid), AP_SSID_PREFIX "%04x", (unsigned)(mac & 0xFFFF));
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(ssid, AP_PASSWORD)) {
        Serial.println("[WiFi] softAP FAILED");
        return;
    }
    Serial.printf("[WiFi] AP up: SSID=%s  pass=%s  IP=%s\n",
                  ssid, AP_PASSWORD, WiFi.softAPIP().toString().c_str());
    start_http_server();
}
#elif PETBOT_ENABLE_WIFI
static void setup_wifi() {
    pinMode(0, INPUT_PULLUP);
    if (digitalRead(0) == LOW) {
        Serial.println("[WiFi] GPIO0 low — hold 3 s to erase credentials...");
        unsigned long held = millis();
        while (digitalRead(0) == LOW && millis() - held < 3000) delay(50);
        if (millis() - held >= 3000) {
            WiFiManager wm;
            wm.resetSettings();
            Serial.println("[WiFi] Credentials erased — starting setup portal");
        } else {
            Serial.println("[WiFi] GPIO0 released early — skipping reset");
        }
    }

    WiFiManager wm;
    wm.setConfigPortalSSID(WIFI_SETUP_SSID);
    wm.setConfigPortalPassword(WIFI_SETUP_PASS);
    wm.setConfigPortalTimeout(180);
    wm.setConnectTimeout(20);

    Serial.println("[WiFi] Connecting (or starting setup portal)...");
    if (!wm.autoConnect(WIFI_SETUP_SSID, WIFI_SETUP_PASS)) {
        Serial.println("[WiFi] Failed — restarting");
        ESP.restart();
    }
    Serial.print("[WiFi] Connected — IP: "); Serial.println(WiFi.localIP());

    if (MDNS.begin(MDNS_NAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("[mDNS] http://" MDNS_NAME ".local");
    }
    start_http_server();
}
#else
static void setup_wifi() {
    Serial.println("[WiFi] disabled (build with -DPETBOT_AP_MODE=1 or -DPETBOT_ENABLE_WIFI=1)");
}
#endif

// ─── BLE wiring ──────────────────────────────────────────────────────────────
#if PETBOT_USE_NIMBLE
class BleServerCB : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*) override {
        s_ble_connected = true; Serial.println("[BLE] connected"); send("READY:PetBot");
    }
    void onDisconnect(NimBLEServer*) override {
        s_ble_connected = false; Serial.println("[BLE] disconnected");
        NimBLEDevice::startAdvertising();
    }
};
class BleRxCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        auto v = c->getValue(); if (v.length()) handleCommand(String(v.c_str()));
    }
};
#else
class BleServerCB : public BLEServerCallbacks {
    void onConnect(BLEServer*) override {
        s_ble_connected = true; Serial.println("[BLE] connected"); send("READY:PetBot");
    }
    void onDisconnect(BLEServer*) override {
        s_ble_connected = false; Serial.println("[BLE] disconnected");
        s_ble_server->startAdvertising();
    }
};
class BleRxCB : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c) override {
        auto v = c->getValue(); if (v.length()) handleCommand(String(v.c_str()));
    }
};
#endif

static void setup_ble() {
  #if PETBOT_USE_NIMBLE
    NimBLEDevice::init(BLE_DEVICE_NAME);
    s_ble_server = NimBLEDevice::createServer();
  #else
    BLEDevice::init(BLE_DEVICE_NAME);
    s_ble_server = BLEDevice::createServer();
  #endif
    s_ble_server->setCallbacks(new BleServerCB());
    PetBleService* svc = s_ble_server->createService(NUS_SERVICE_UUID);
  #if PETBOT_USE_NIMBLE
    const uint32_t rx_props = NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR;
    const uint32_t tx_props = NIMBLE_PROPERTY::NOTIFY;
  #else
    const uint32_t rx_props = BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR;
    const uint32_t tx_props = BLECharacteristic::PROPERTY_NOTIFY;
  #endif
    PetBleCharacteristic* rx = svc->createCharacteristic(NUS_RX_UUID, rx_props);
    rx->setCallbacks(new BleRxCB());
    s_ble_notify = svc->createCharacteristic(NUS_TX_UUID, tx_props);
  #if !PETBOT_USE_NIMBLE
    s_ble_notify->addDescriptor(new BLE2902());
  #endif
    svc->start();
  #if PETBOT_USE_NIMBLE
    PetBleAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SERVICE_UUID); adv->setScanResponse(true);
    adv->setName(BLE_DEVICE_NAME); NimBLEDevice::startAdvertising();
  #else
    PetBleAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SERVICE_UUID); adv->setScanResponse(true);
    { BLEAdvertisementData sd; sd.setName(BLE_DEVICE_NAME); adv->setScanResponseData(sd); }
    BLEDevice::startAdvertising();
  #endif
    Serial.println("[BLE] Advertising as \"" BLE_DEVICE_NAME "\"");
}

void init() {
    setup_wifi();
    setup_ble();
}

}  // namespace ble_web
