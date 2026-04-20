/*
 * PetBot — ESP32-CAM firmware
 *
 * Board  : AI-Thinker ESP32-CAM (OV2640)
 * Control: BLE Nordic UART Service (NUS)
 *
 * HOW TO CONNECT
 *   Desktop/Android : open web/robot_webapp.html in Chrome or Edge,
 *                     click Connect, select PetBot.
 *   iPhone/iPad     : use the free "nRF Connect" app, connect to PetBot,
 *                     subscribe to TX (6E400003…), write commands to RX (6E400002…).
 *
 * OPTIONAL CAMERA STREAM
 *   Compile with -DPETBOT_ENABLE_STREAM=1 and set partition to
 *   "Huge APP (3MB No OTA)" — adds WiFi AP + MJPEG at http://192.168.4.1/stream
 *
 * ADD YOUR HARDWARE
 *   Search for the four STUB sections below. Set the matching
 *   #define to 1 and fill the TODO bodies to wire in:
 *     SCREEN   – SPI/I2C display
 *     MOTORS   – motor driver (TB6612, DRV8833, L298N …)
 *     MIC      – I2S microphone (INMP441 …)
 *     SPEAKER  – I2S amplifier (MAX98357A …)
 */

#include <Arduino.h>

// ─── Optional camera/stream (needs Huge APP partition) ───────────────────────
#ifndef PETBOT_ENABLE_STREAM
#define PETBOT_ENABLE_STREAM 0
#endif

#if PETBOT_ENABLE_STREAM
#include <WiFi.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#endif

// ─── BLE library (uses NimBLE when installed — smaller flash footprint) ───────
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

// ─── Identity ────────────────────────────────────────────────────────────────
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

void setupSpeaker()              { /* TODO: install I2S driver */ }
void speakText(const String& t)  { Serial.print("[SPEAK] "); Serial.println(t); /* TODO */ }
void playSound(const String& n)  { Serial.print("[SOUND] "); Serial.println(n); /* TODO */ }

// ═════════════════════════════════════════════════════════════════════════════
//  Camera + MJPEG stream  (only compiled with -DPETBOT_ENABLE_STREAM=1)
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
    cfg.pin_pwdn = CAM_PWDN;  cfg.pin_reset = CAM_RESET;
    cfg.xclk_freq_hz = 20000000; cfg.pixel_format = PIXFORMAT_JPEG;
    cfg.frame_size = FRAMESIZE_QVGA; cfg.jpeg_quality = 10; cfg.fb_count = 2;
    if (esp_camera_init(&cfg) != ESP_OK)
        Serial.println("[CAM] Init FAILED — check wiring");
    else
        Serial.println("[CAM] OV2640 ready");
}

httpd_handle_t streamHttpd = nullptr;

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

void setupWifiAndStream() {
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    Serial.print("[WiFi] AP IP: "); Serial.println(WiFi.softAPIP());
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&streamHttpd, &cfg) == ESP_OK) {
        httpd_uri_t su = { "/stream", HTTP_GET, handleStream, nullptr };
        httpd_register_uri_handler(streamHttpd, &su);
        Serial.println("[HTTP] /stream active on port 80");
    }
}
#else
void setupCamera()        { Serial.println("[CAM] disabled"); }
void setupWifiAndStream() { Serial.println("[WiFi] disabled"); }
#endif

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
      #if PETBOT_ENABLE_STREAM
        s += ",stream=http://" WIFI_AP_IP "/stream";
      #else
        s += ",stream=disabled";
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
    Serial.println("  BLE : connect to \"" BLE_DEVICE_NAME "\"");
  #if PETBOT_ENABLE_STREAM
    Serial.println("  Cam : http://" WIFI_AP_IP "/stream  (join " WIFI_AP_SSID ")");
  #endif
}

void loop() {
  #if MIC_ENABLED
    String heard = micListen();
    if (heard.length()) handleCommand(heard);
  #endif
    delay(10);
}
