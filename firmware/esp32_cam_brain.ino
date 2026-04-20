/*
 * PetBot — ESP32-CAM firmware
 *
 * Board  : AI-Thinker ESP32-CAM (OV2640)
 * Control: BLE Nordic UART Service + WiFi captive portal
 *
 * ── iPAD / iPHONE ────────────────────────────────────────────────────────────
 *  Compile with -DPETBOT_ENABLE_WIFI=1  (default partition is fine)
 *  1. Flash and power on the ESP32.
 *  2. On iPad go to Settings → Wi-Fi → join "PETBOT_CAM" (password: petbot123).
 *  3. iOS detects the captive portal and opens the control page automatically.
 *  That's it — no app, no URL to type.
 *
 * ── DESKTOP / ANDROID ────────────────────────────────────────────────────────
 *  Default build (no extra flags). Open web/robot_webapp.html in Chrome or
 *  Edge, click Connect, select PetBot from the Bluetooth picker.
 *
 * ── CAMERA STREAM ─────────────────────────────────────────────────────────────
 *  Add -DPETBOT_ENABLE_STREAM=1 + "Huge APP (3MB)" partition scheme.
 *  Stream available at http://192.168.4.1/stream after joining PETBOT_CAM.
 *
 * ── ADD YOUR HARDWARE ─────────────────────────────────────────────────────────
 *  Search for the four STUB sections. Set the matching #define to 1 and fill
 *  the TODO bodies to wire in SCREEN, MOTORS, MIC, or SPEAKER.
 */

#include <Arduino.h>

// ─── Feature flags ────────────────────────────────────────────────────────────
#ifndef PETBOT_ENABLE_STREAM
#define PETBOT_ENABLE_STREAM 0
#endif
#ifndef PETBOT_ENABLE_WIFI
#define PETBOT_ENABLE_WIFI 1  // set 0 to disable WiFi captive portal
#endif

#if PETBOT_ENABLE_WIFI
#include <WiFi.h>
#include <DNSServer.h>
#include "esp_http_server.h"
#endif
#if PETBOT_ENABLE_STREAM
#include "esp_camera.h"
#endif

// ─── BLE library ─────────────────────────────────────────────────────────────
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

// ─── Identity ─────────────────────────────────────────────────────────────────
#define BLE_DEVICE_NAME  "PetBot"
#define WIFI_AP_SSID     "PETBOT_CAM"
#define WIFI_AP_PASS     "petbot123"
#define WIFI_AP_IP       "192.168.4.1"

// ─── BLE Nordic UART Service UUIDs ───────────────────────────────────────────
#define NUS_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // app → bot
#define NUS_TX_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // bot → app

// ─── Camera pins (AI-Thinker ESP32-CAM) ──────────────────────────────────────
#define CAM_PWDN   32
#define CAM_RESET  -1
#define CAM_XCLK    0
#define CAM_SIOD   26
#define CAM_SIOC   27
#define CAM_D7     35
#define CAM_D6     34
#define CAM_D5     39
#define CAM_D4     36
#define CAM_D3     21
#define CAM_D2     19
#define CAM_D1     18
#define CAM_D0      5
#define CAM_VSYNC  25
#define CAM_HREF   23
#define CAM_PCLK   22

// ═════════════════════════════════════════════════════════════════════════════
//  STUB: SCREEN — set SCREEN_ENABLED 1, pick a library, fill the TODOs
// ═════════════════════════════════════════════════════════════════════════════
#define SCREEN_ENABLED 0
// #include <TFT_eSPI.h>
// TFT_eSPI tft;

void setupScreen() {
    // TODO: tft.init(); tft.setRotation(1); tft.fillScreen(TFT_BLACK);
}
void screenShow(const String& msg) {
    // TODO: tft.fillScreen(TFT_BLACK); tft.setCursor(10,10); tft.print(msg);
}

// ═════════════════════════════════════════════════════════════════════════════
//  STUB: MOTORS — set MOTORS_ENABLED 1, define pins, fill the TODOs
// ═════════════════════════════════════════════════════════════════════════════
#define MOTORS_ENABLED 0
// #define M_AIN1 12
// #define M_AIN2 13
// #define M_BIN1 14
// #define M_BIN2 15

void setupMotors()   { /* TODO: pinMode + ledcSetup */ }
void motorsForward() { /* TODO */ }
void motorsBack()    { /* TODO */ }
void motorsLeft()    { /* TODO */ }
void motorsRight()   { /* TODO */ }
void motorsStop()    { /* TODO */ }

// ═════════════════════════════════════════════════════════════════════════════
//  STUB: MIC — set MIC_ENABLED 1, define I2S pins, fill the TODO
// ═════════════════════════════════════════════════════════════════════════════
#define MIC_ENABLED 0
// #define MIC_SCK 14
// #define MIC_WS  15
// #define MIC_SD  32

void setupMic() { /* TODO: install I2S driver */ }
String micListen() { return ""; /* TODO: read + VAD */ }

// ═════════════════════════════════════════════════════════════════════════════
//  STUB: SPEAKER — set SPEAKER_ENABLED 1, define I2S pins, fill the TODOs
// ═════════════════════════════════════════════════════════════════════════════
#define SPEAKER_ENABLED 0
// #define SPK_BCK  26
// #define SPK_WS   25
// #define SPK_DATA 33

void setupSpeaker()             { /* TODO: install I2S driver */ }
void speakText(const String& t) { Serial.print("[SPEAK] "); Serial.println(t); }
void playSound(const String& n) { Serial.print("[SOUND] "); Serial.println(n); }

// ═════════════════════════════════════════════════════════════════════════════
//  WiFi AP + Captive Portal + Web UI
// ═════════════════════════════════════════════════════════════════════════════
#if PETBOT_ENABLE_WIFI

static const char WEBAPP_HTML[] =
    "<!DOCTYPE html><html><head>"
    "<meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>PetBot</title><style>*{box-sizing:border-box}"
    "body{font-family:sans-serif;background:#1a1a2e;color:#fff;max-width:360px;margin:0 auto;padding:16px}"
    "h1{text-align:center;color:#e94560}"
    ".g{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin:12px 0}"
    "button{background:#16213e;color:#fff;border:2px solid #e94560;border-radius:8px;"
    "padding:14px;font-size:22px;cursor:pointer;-webkit-tap-highlight-color:transparent}"
    "button:active{background:#e94560}.r{display:flex;gap:8px;margin-top:8px}"
    "input{flex:1;padding:8px;background:#16213e;color:#fff;border:2px solid #e94560;border-radius:8px}"
    "#st{padding:6px;border-radius:4px;background:#16213e;margin:8px 0;font-size:13px}"
    "</style></head><body><h1>PetBot</h1><div id=st>Connected</div>"
    "<div class=g><i></i>"
    "<button ontouchstart=\"go('MOVE:fwd')\" ontouchend=\"go('MOVE:stop')\">&#9650;</button><i></i>"
    "<button ontouchstart=\"go('MOVE:left')\" ontouchend=\"go('MOVE:stop')\">&#9664;</button>"
    "<button onclick=\"go('MOVE:stop')\">&#9632;</button>"
    "<button ontouchstart=\"go('MOVE:right')\" ontouchend=\"go('MOVE:stop')\">&#9654;</button>"
    "<i></i><button ontouchstart=\"go('MOVE:back')\" ontouchend=\"go('MOVE:stop')\">&#9660;</button>"
    "<i></i></div><div class=r><input id=t placeholder='Type to speak...'>"
    "<button onclick=\"go('SAY:'+document.getElementById('t').value)\">&#128263;</button>"
    "</div><script>function go(c){fetch('/cmd?c='+encodeURIComponent(c))"
    ".then(r=>r.text()).then(t=>document.getElementById('st').textContent=t)"
    ".catch(()=>document.getElementById('st').textContent='error')}</script></body></html>";

DNSServer      dnsServer;
httpd_handle_t webHttpd = nullptr;

static esp_err_t handleWebRoot(httpd_req_t* r) {
    httpd_resp_set_type(r, "text/html");
    httpd_resp_sendstr(r, WEBAPP_HTML);
    return ESP_OK;
}

// Catch-all for captive portal detection URLs (captive.apple.com, etc.)
static esp_err_t handlePortal(httpd_req_t* r, httpd_err_code_t) {
    httpd_resp_set_type(r, "text/html");
    httpd_resp_sendstr(r, WEBAPP_HTML);
    return ESP_OK;
}

void handleCommand(const String&);  // defined after BLE section

static esp_err_t handleCmd(httpd_req_t* r) {
    char q[80] = {};
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK) {
        char v[64] = {};
        if (httpd_query_key_value(q, "c", v, sizeof(v)) == ESP_OK)
            handleCommand(String(v));
    }
    httpd_resp_sendstr(r, "ok");
    return ESP_OK;
}
#endif  // PETBOT_ENABLE_WIFI

// ═════════════════════════════════════════════════════════════════════════════
//  Camera + MJPEG stream
// ═════════════════════════════════════════════════════════════════════════════
#if PETBOT_ENABLE_STREAM
void setupCamera() {
    camera_config_t cfg = {};
    cfg.ledc_channel = LEDC_CHANNEL_0; cfg.ledc_timer  = LEDC_TIMER_0;
    cfg.pin_d0 = CAM_D0; cfg.pin_d1 = CAM_D1; cfg.pin_d2 = CAM_D2;
    cfg.pin_d3 = CAM_D3; cfg.pin_d4 = CAM_D4; cfg.pin_d5 = CAM_D5;
    cfg.pin_d6 = CAM_D6; cfg.pin_d7 = CAM_D7;
    cfg.pin_xclk = CAM_XCLK; cfg.pin_pclk = CAM_PCLK;
    cfg.pin_vsync = CAM_VSYNC; cfg.pin_href = CAM_HREF;
    cfg.pin_sscb_sda = CAM_SIOD; cfg.pin_sscb_scl = CAM_SIOC;
    cfg.pin_pwdn = CAM_PWDN; cfg.pin_reset = CAM_RESET;
    cfg.xclk_freq_hz = 20000000; cfg.pixel_format = PIXFORMAT_JPEG;
    cfg.frame_size = FRAMESIZE_QVGA; cfg.jpeg_quality = 10; cfg.fb_count = 2;
    if (esp_camera_init(&cfg) != ESP_OK)
        Serial.println("[CAM] Init FAILED — check wiring");
    else
        Serial.println("[CAM] OV2640 ready");
}

static esp_err_t handleStream(httpd_req_t* req) {
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
#else
void setupCamera() { Serial.println("[CAM] disabled"); }
#endif  // PETBOT_ENABLE_STREAM

void setupWifiAndStream() {
#if PETBOT_ENABLE_WIFI
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    Serial.print("[WiFi] AP IP: "); Serial.println(WiFi.softAPIP());

    // DNS: redirect every hostname to us so iOS captive portal detection fires
    dnsServer.start(53, "*", WiFi.softAPIP());

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&webHttpd, &cfg) == ESP_OK) {
        httpd_uri_t ru = { "/",    HTTP_GET, handleWebRoot, nullptr };
        httpd_uri_t cu = { "/cmd", HTTP_GET, handleCmd,     nullptr };
        httpd_register_uri_handler(webHttpd, &ru);
        httpd_register_uri_handler(webHttpd, &cu);
        // Serve the webapp for any path iOS/Android uses for captive-portal checks
        httpd_register_err_handler(webHttpd, HTTPD_404_NOT_FOUND, handlePortal);
  #if PETBOT_ENABLE_STREAM
        httpd_uri_t su = { "/stream", HTTP_GET, handleStream, nullptr };
        httpd_register_uri_handler(webHttpd, &su);
        Serial.println("[HTTP] Captive portal + /cmd + /stream active");
  #else
        Serial.println("[HTTP] Captive portal + /cmd active");
  #endif
    }
#else
    Serial.println("[WiFi] disabled — compile with -DPETBOT_ENABLE_WIFI=1 for iPad captive portal");
#endif
}

// ═════════════════════════════════════════════════════════════════════════════
//  BLE — Nordic UART Service
// ═════════════════════════════════════════════════════════════════════════════
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

PetBleServer*         pBleServer = nullptr;
PetBleCharacteristic* pBleNotify = nullptr;
bool                  bleConnected = false;

void bleSend(const String& msg) {
    if (!bleConnected || !pBleNotify) return;
    pBleNotify->setValue(msg.c_str());
    pBleNotify->notify();
}

void handleCommand(const String& cmd);  // forward declaration

#if PETBOT_USE_NIMBLE
class BleServerCB : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*) override {
        bleConnected = true;
        Serial.println("[BLE] connected");
        bleSend("READY:PetBot");
    }
    void onDisconnect(NimBLEServer*) override {
        bleConnected = false;
        Serial.println("[BLE] disconnected — restarting advertising");
        NimBLEDevice::startAdvertising();
    }
};
class BleRxCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        auto v = c->getValue();
        if (v.length()) handleCommand(String(v.c_str()));
    }
};
#else
class BleServerCB : public BLEServerCallbacks {
    void onConnect(BLEServer*) override {
        bleConnected = true;
        Serial.println("[BLE] connected");
        bleSend("READY:PetBot");
    }
    void onDisconnect(BLEServer*) override {
        bleConnected = false;
        Serial.println("[BLE] disconnected — restarting advertising");
        pBleServer->startAdvertising();
    }
};
class BleRxCB : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c) override {
        auto v = c->getValue();
        if (v.length()) handleCommand(String(v.c_str()));
    }
};
#endif

void setupBLE() {
  #if PETBOT_USE_NIMBLE
    NimBLEDevice::init(BLE_DEVICE_NAME);
    pBleServer = NimBLEDevice::createServer();
  #else
    BLEDevice::init(BLE_DEVICE_NAME);
    pBleServer = BLEDevice::createServer();
  #endif
    pBleServer->setCallbacks(new BleServerCB());

    PetBleService* svc = pBleServer->createService(NUS_SERVICE_UUID);

  #if PETBOT_USE_NIMBLE
    const uint32_t rxProps = NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR;
    const uint32_t txProps = NIMBLE_PROPERTY::NOTIFY;
  #else
    const uint32_t rxProps = BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR;
    const uint32_t txProps = BLECharacteristic::PROPERTY_NOTIFY;
  #endif

    PetBleCharacteristic* rx = svc->createCharacteristic(NUS_RX_UUID, rxProps);
    rx->setCallbacks(new BleRxCB());

    pBleNotify = svc->createCharacteristic(NUS_TX_UUID, txProps);
  #if !PETBOT_USE_NIMBLE
    pBleNotify->addDescriptor(new BLE2902());
  #endif

    svc->start();

  #if PETBOT_USE_NIMBLE
    PetBleAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SERVICE_UUID);
    adv->setScanResponse(true);
    adv->setName(BLE_DEVICE_NAME);
    NimBLEDevice::startAdvertising();
  #else
    PetBleAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SERVICE_UUID);
    adv->setScanResponse(true);
    { BLEAdvertisementData sd; sd.setName(BLE_DEVICE_NAME); adv->setScanResponseData(sd); }
    BLEDevice::startAdvertising();
  #endif
    Serial.println("[BLE] Advertising as \"" BLE_DEVICE_NAME "\"");
}

// ═════════════════════════════════════════════════════════════════════════════
//  Command dispatcher
// ═════════════════════════════════════════════════════════════════════════════
void handleCommand(const String& cmd) {
    Serial.print("[CMD] "); Serial.println(cmd);

    if (cmd.startsWith("MOVE:")) {
        String dir = cmd.substring(5);
        if      (dir == "fwd")   motorsForward();
        else if (dir == "back")  motorsBack();
        else if (dir == "left")  motorsLeft();
        else if (dir == "right") motorsRight();
        else                     motorsStop();
        bleSend("OK:MOVE:" + dir);

    } else if (cmd.startsWith("SAY:")) {
        speakText(cmd.substring(4));
        bleSend("OK:SAY");

    } else if (cmd.startsWith("SOUND:")) {
        playSound(cmd.substring(6));
        bleSend("OK:SOUND");

    } else if (cmd.startsWith("SCREEN:")) {
        screenShow(cmd.substring(7));
        bleSend("OK:SCREEN");

    } else if (cmd == "STATUS") {
        String s = "STATUS:ok";
        s += ",motors=";  s += MOTORS_ENABLED  ? "1" : "0";
        s += ",screen=";  s += SCREEN_ENABLED   ? "1" : "0";
        s += ",mic=";     s += MIC_ENABLED      ? "1" : "0";
        s += ",speaker="; s += SPEAKER_ENABLED  ? "1" : "0";
      #if PETBOT_ENABLE_WIFI
        s += ",web=http://" WIFI_AP_IP;
      #endif
        bleSend(s);

    } else {
        bleSend("ERR:unknown:" + cmd);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Setup & loop
// ═════════════════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    Serial.println("\n=== PetBot booting ===");

    setupCamera();
    setupWifiAndStream();
    setupBLE();

    if (SCREEN_ENABLED)  setupScreen();
    if (MOTORS_ENABLED)  setupMotors();
    if (MIC_ENABLED)     setupMic();
    if (SPEAKER_ENABLED) setupSpeaker();

    Serial.println("=== PetBot ready ===");
  #if PETBOT_ENABLE_WIFI
    Serial.println("  iPad : join Wi-Fi \"" WIFI_AP_SSID "\" / " WIFI_AP_PASS " — page opens automatically");
  #endif
    Serial.println("  BLE  : connect to \"" BLE_DEVICE_NAME "\" via Chrome/Edge web app");
}

void loop() {
  #if PETBOT_ENABLE_WIFI
    dnsServer.processNextRequest();
  #endif
  #if MIC_ENABLED
    String heard = micListen();
    if (heard.length()) handleCommand(heard);
  #endif
    delay(10);
}
