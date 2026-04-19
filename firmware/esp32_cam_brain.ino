/*
 * PetBot — ESP32-CAM main firmware
 *
 * Board  : AI-Thinker ESP32-CAM (OV2640)
 * Control: BLE (Nordic UART Service) ← webapp
 * Stream : MJPEG over WiFi AP  →  http://192.168.4.1/stream
 *
 * ── ADD YOUR HARDWARE LATER ──────────────────────────────────────────────
 *  Search for the four stub sections below and wire in:
 *    SCREEN   – SPI/I2C display
 *    MOTORS   – motor driver (TB6612, DRV8833, L298N …)
 *    MIC      – I2S microphone (INMP441 …)
 *    SPEAKER  – I2S amplifier (MAX98357A …)
 *  Set the matching #define to 1 and fill the TODO bodies.
 * ─────────────────────────────────────────────────────────────────────────
 */

#include <Arduino.h>

// Build-time feature gates for fitting default ESP32-CAM app partitions.
// Compile with -DPETBOT_ENABLE_STREAM=1 if you use a larger app partition (e.g. Huge APP).
#ifndef PETBOT_ENABLE_STREAM
#define PETBOT_ENABLE_STREAM 0
#endif

#if PETBOT_ENABLE_STREAM
    #include <WiFi.h>
    #include "esp_camera.h"
    #include "esp_http_server.h"
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

// ─── Identity ────────────────────────────────────────────────────────────────
#define BLE_DEVICE_NAME  "PetBot"
#define WIFI_AP_SSID     "PETBOT_CAM"
#define WIFI_AP_PASS     "petbot123"

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
//  STUB: SCREEN
//  Set SCREEN_ENABLED to 1, pick your library, fill the two TODO bodies.
// ═════════════════════════════════════════════════════════════════════════════
#define SCREEN_ENABLED 0

// #include <TFT_eSPI.h>          // example: ST7789 via SPI
// TFT_eSPI tft;

void setupScreen() {
    // TODO — init your display
    // tft.init();
    // tft.setRotation(1);
    // tft.fillScreen(TFT_BLACK);
}

void screenShow(const String& msg) {
    // TODO — draw text / face on your display
    // tft.fillScreen(TFT_BLACK);
    // tft.setCursor(10, 10);
    // tft.print(msg);
}

// ═════════════════════════════════════════════════════════════════════════════
//  STUB: MOTORS
//  Set MOTORS_ENABLED to 1, define your driver pins, fill the TODO bodies.
// ═════════════════════════════════════════════════════════════════════════════
#define MOTORS_ENABLED 0

// #define M_AIN1  12   // ← set pins for your wiring
// #define M_AIN2  13
// #define M_BIN1  14
// #define M_BIN2  15
// #define M_PWMA   2
// #define M_PWMB   4

void setupMotors() {
    // TODO — configure motor driver GPIO and PWM channels
}
void motorsForward()  { /* TODO */ }
void motorsBack()     { /* TODO */ }
void motorsLeft()     { /* TODO */ }
void motorsRight()    { /* TODO */ }
void motorsStop()     { /* TODO */ }

// ═════════════════════════════════════════════════════════════════════════════
//  STUB: MIC
//  Set MIC_ENABLED to 1, pick your I2S pins, fill the TODO body.
// ═════════════════════════════════════════════════════════════════════════════
#define MIC_ENABLED 0

// #define MIC_SCK  14   // I2S bit clock
// #define MIC_WS   15   // I2S word select
// #define MIC_SD   32   // I2S data

void setupMic() {
    // TODO — install I2S driver for microphone input
}

String micListen() {
    // TODO — read audio buffer, run VAD / keyword detection, return command string
    return "";
}

// ═════════════════════════════════════════════════════════════════════════════
//  STUB: SPEAKER
//  Set SPEAKER_ENABLED to 1, pick your I2S pins, fill the TODO bodies.
// ═════════════════════════════════════════════════════════════════════════════
#define SPEAKER_ENABLED 0

// #define SPK_BCK   26   // I2S bit clock
// #define SPK_WS    25   // I2S word select (LR)
// #define SPK_DATA  33   // I2S data out

void setupSpeaker() {
    // TODO — install I2S driver for speaker output
}

void speakText(const String& text) {
    // TODO — convert text to audio (TTS or tone sequence) and send via I2S
    Serial.print("[SPEAK] "); Serial.println(text);
}

void playSound(const String& name) {
    // TODO — play a named sound effect (boot, happy, alert …)
    Serial.print("[SOUND] "); Serial.println(name);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Camera
// ═════════════════════════════════════════════════════════════════════════════
#if PETBOT_ENABLE_STREAM
void setupCamera() {
    camera_config_t cfg = {};
    cfg.ledc_channel  = LEDC_CHANNEL_0;
    cfg.ledc_timer    = LEDC_TIMER_0;
    cfg.pin_d0        = CAM_D0;
    cfg.pin_d1        = CAM_D1;
    cfg.pin_d2        = CAM_D2;
    cfg.pin_d3        = CAM_D3;
    cfg.pin_d4        = CAM_D4;
    cfg.pin_d5        = CAM_D5;
    cfg.pin_d6        = CAM_D6;
    cfg.pin_d7        = CAM_D7;
    cfg.pin_xclk      = CAM_XCLK;
    cfg.pin_pclk      = CAM_PCLK;
    cfg.pin_vsync     = CAM_VSYNC;
    cfg.pin_href      = CAM_HREF;
    cfg.pin_sscb_sda  = CAM_SIOD;
    cfg.pin_sscb_scl  = CAM_SIOC;
    cfg.pin_pwdn      = CAM_PWDN;
    cfg.pin_reset     = CAM_RESET;
    cfg.xclk_freq_hz  = 20000000;
    cfg.pixel_format  = PIXFORMAT_JPEG;
    cfg.frame_size    = FRAMESIZE_QVGA;  // 320x240
    cfg.jpeg_quality  = 10;
    cfg.fb_count      = 2;

    if (esp_camera_init(&cfg) != ESP_OK) {
        Serial.println("[CAM] Init FAILED — check wiring");
    } else {
        Serial.println("[CAM] OV2640 ready");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  HTTP MJPEG stream server
// ═════════════════════════════════════════════════════════════════════════════
httpd_handle_t streamHttpd = nullptr;

static esp_err_t handleStream(httpd_req_t* req) {
    camera_fb_t* fb  = nullptr;
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

static esp_err_t handleRoot(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "application/json");
    const char* json = "{\"status\":\"ok\",\"stream\":\"/stream\",\"ble\":\"PetBot\"}";
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

void setupWifiAndStream() {
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    Serial.print("[WiFi] AP ready — IP: ");
    Serial.println(WiFi.softAPIP());

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;

    if (httpd_start(&streamHttpd, &cfg) == ESP_OK) {
        httpd_uri_t streamUri = { "/stream", HTTP_GET, handleStream, nullptr };
        httpd_uri_t rootUri   = { "/",       HTTP_GET, handleRoot,   nullptr };
        httpd_register_uri_handler(streamHttpd, &streamUri);
        httpd_register_uri_handler(streamHttpd, &rootUri);
        Serial.println("[HTTP] /stream  /  active on port 80");
    }
}
#else
// Intentional no-op stubs: setup() always calls these to keep one init flow.
void setupCamera() {
    Serial.println("[CAM] Stream disabled at compile time (PETBOT_ENABLE_STREAM=0)");
}

void setupWifiAndStream() {
    Serial.println("[WiFi] AP/stream disabled at compile time (PETBOT_ENABLE_STREAM=0)");
}
#endif

// ═════════════════════════════════════════════════════════════════════════════
//  BLE
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
        Serial.println("[BLE] Client connected");
        bleSend("READY:PetBot");
    }
    void onDisconnect(NimBLEServer*) override {
        bleConnected = false;
        Serial.println("[BLE] Client disconnected — restarting advertising");
        NimBLEDevice::startAdvertising();
    }
};

class BleRxCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        auto value = c->getValue();
        if (value.length()) handleCommand(String(value.c_str()));
    }
};
#else
class BleServerCB : public BLEServerCallbacks {
    void onConnect(BLEServer*) override {
        bleConnected = true;
        Serial.println("[BLE] Client connected");
        bleSend("READY:PetBot");
    }
    void onDisconnect(BLEServer*) override {
        bleConnected = false;
        Serial.println("[BLE] Client disconnected — restarting advertising");
        pBleServer->startAdvertising();
    }
};

class BleRxCB : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c) override {
        auto value = c->getValue();
        if (value.length()) handleCommand(String(value.c_str()));
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
    // Classic BLE needs an explicit CCCD descriptor for client notifications.
    pBleNotify->addDescriptor(new BLE2902());
#endif

    svc->start();
  #if PETBOT_USE_NIMBLE
    PetBleAdvertising* adv = NimBLEDevice::getAdvertising();
  #else
    PetBleAdvertising* adv = BLEDevice::getAdvertising();
  #endif
    adv->addServiceUUID(NUS_SERVICE_UUID);
    adv->setScanResponse(true);
  #if PETBOT_USE_NIMBLE
    NimBLEDevice::startAdvertising();
  #else
    BLEDevice::startAdvertising();
  #endif
    Serial.println("[BLE] Advertising as \"" BLE_DEVICE_NAME "\"");
}

// ═════════════════════════════════════════════════════════════════════════════
//  Command dispatcher  (BLE → motors / speaker / screen)
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
        s += ",stream=http://192.168.4.1/stream";
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

    if (SCREEN_ENABLED)   setupScreen();
    if (MOTORS_ENABLED)   setupMotors();
    if (MIC_ENABLED)      setupMic();
    if (SPEAKER_ENABLED)  setupSpeaker();

    Serial.println("=== PetBot ready ===");
    Serial.println("  BLE  : connect to \"" BLE_DEVICE_NAME "\"");
#if PETBOT_ENABLE_STREAM
    Serial.println("  WiFi : join \"" WIFI_AP_SSID "\" / " WIFI_AP_PASS);
    Serial.println("  Cam  : http://192.168.4.1/stream");
#else
    Serial.println("  Cam  : disabled (compile with -DPETBOT_ENABLE_STREAM=1 for AP+stream build)");
#endif
}

void loop() {
    // BLE commands arrive via BleRxCB — nothing to poll here.

#if MIC_ENABLED
    String heard = micListen();
    if (heard.length()) handleCommand(heard);
#endif

    delay(10);
}
