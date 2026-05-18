// Auto-consolidated firmware for ESP32-S3-CAM (brain)
// Generated from former src/*.cpp modules.

// Target: ESP32-S3-CAM (brain)
// Holds expression / mode state and routes side effects to the relevant
// subsystem modules. Cross-module calls live here so menu_controller and
// ble_web don't have to know about each other.

#include "app_state.h"

#include "audio_player.h"
#include "face_render.h"
#include "menu_controller.h"
#include "motor_driver.h"

namespace app_state {

static FaceMode g_expr = FaceMode::IDLE;
static CtrlMode g_mode = CtrlMode::MANUAL;

void init() {
    g_expr = FaceMode::IDLE;
    g_mode = CtrlMode::MANUAL;
}

void setExpression(FaceMode m) {
    if (m == g_expr) return;
    g_expr = m;
    face_render::onExpressionChanged(m);
    menu_controller::onStateChanged();
}

FaceMode expression() { return g_expr; }

void setMode(CtrlMode m) {
    if (m == g_mode) return;
    g_mode = m;
    menu_controller::onStateChanged();
}

CtrlMode mode() { return g_mode; }

void moveForward() { motor_driver::forward(); }
void moveBack()    { motor_driver::back(); }
void moveLeft()    { motor_driver::left(); }
void moveRight()   { motor_driver::right(); }
void moveStop()    { motor_driver::stop(); }

void say(const char* text)       { audio_player::say(text); }
void playSound(const char* name) { audio_player::play(name); }

const char* expressionName(FaceMode m) {
    switch (m) {
        case FaceMode::IDLE:    return "IDLE";
        case FaceMode::HAPPY:   return "HAPPY";
        case FaceMode::SEARCH:  return "SEARCH";
        case FaceMode::CURIOUS: return "CURIOUS";
        case FaceMode::DRIVE:   return "DRIVE";
        case FaceMode::SLEEP:   return "SLEEP";
        default:                return "?";
    }
}

const char* modeName(CtrlMode m) {
    switch (m) {
        case CtrlMode::MANUAL:  return "manual";
        case CtrlMode::AUTO:    return "auto";
        default:                return "?";
    }
}

}  // namespace app_state

// Target: ESP32-S3-CAM (brain)
// Audio stubs. Set SPEAKER_ENABLED 1 once the I2S pins for MAX98357A are
// known — BCLK, LRC, DIN. Picking pins after the camera bus is finalised
// avoids reshuffling later.

#include "audio_player.h"

#include <Arduino.h>

#define SPEAKER_ENABLED 0

namespace audio_player {

bool enabled() { return SPEAKER_ENABLED != 0; }

void init() {
#if SPEAKER_ENABLED
    // TODO: i2s_driver_install + pin config.
#endif
}

void say(const char* text) {
    Serial.print("[say] "); Serial.println(text ? text : "");
#if SPEAKER_ENABLED
    // TODO: feed synth output into i2s_write.
#endif
}

void play(const char* name) {
    Serial.print("[sound] "); Serial.println(name ? name : "");
#if SPEAKER_ENABLED
    // TODO: look up `name` in the sound bank and play it.
#endif
}

}  // namespace audio_player

// Target: ESP32-S3-CAM (brain)
// Big face TFT renderer. Expects the face TFT to live on FSPI / SPI3_HOST
// so the camera's parallel bus + dedicated SCCB stay untouched.
//
// Stubbed at compile-time until you commit to a specific face TFT
// (ST7789 vs ILI9341, plus pin map). The state-tracking + blink timing
// run regardless — they just don't push pixels — so the brain logic can
// be exercised before the panel is wired.

#include "face_render.h"

#include <Arduino.h>

#define FACE_TFT_ENABLED 0

#if FACE_TFT_ENABLED
  // Suggested wiring — adjust to match your S3-CAM carrier's free pins:
  //   #define FACE_PIN_MOSI  ?
  //   #define FACE_PIN_SCLK  ?
  //   #define FACE_PIN_CS    ?
  //   #define FACE_PIN_DC    ?
  //   #define FACE_PIN_RST   ?
  //   #define FACE_PIN_BL    ?
  // and either Adafruit_GFX + Adafruit_ST7789 or TFT_eSPI here.
#endif

namespace face_render {

static FaceMode      g_mode         = FaceMode::IDLE;
static unsigned long g_last_blink   = 0;
static bool          g_blink_active = false;

bool enabled() { return FACE_TFT_ENABLED != 0; }

void init() {
#if FACE_TFT_ENABLED
    // TODO: SPI bus (FSPI/SPI3_HOST) begin + tft.init(...) + setRotation(?).
#endif
    Serial.printf("[face] init (enabled=%d)\n", FACE_TFT_ENABLED);
}

static void render() {
#if FACE_TFT_ENABLED
    // TODO: Use the renderFace() patterns from §6 of ROBOT_FIRMWARE_PLAN.md
    // — fillScreen(bgForMode), then fillRoundRect for eyes (collapse eyeH
    // when g_blink_active), then per-mode mouth / cheeks switch.
#else
    Serial.printf("[face] render mode=%d blink=%d\n", (int)g_mode, g_blink_active);
#endif
}

void onExpressionChanged(FaceMode m) {
    g_mode = m;
    g_blink_active = false;
    render();
}

void update() {
    unsigned long now = millis();
    // Simple blink cadence: ~3 s between blinks, blink lasts ~120 ms.
    if (g_blink_active) {
        if (now - g_last_blink > 120) {
            g_blink_active = false;
            render();
        }
    } else {
        if (now - g_last_blink > 3000 && g_mode != FaceMode::SLEEP) {
            g_blink_active = true;
            g_last_blink = now;
            render();
        }
    }
}

}  // namespace face_render

// Target: ESP32-S3-CAM (brain)
// Motor stubs. To wire real motors:
//   1. Set MOTORS_ENABLED to 1.
//   2. Pick TB6612FNG-compatible GPIOs (avoid camera bus, face-TFT bus,
//      and the S3 UART pins used for the C6 link later in this sketch).
//   3. Fill the TODO bodies.

#include "motor_driver.h"

#include <Arduino.h>

#define MOTORS_ENABLED 0

namespace motor_driver {

bool enabled() { return MOTORS_ENABLED != 0; }

void init() {
#if MOTORS_ENABLED
    // TODO: pinMode(M_AIN1, OUTPUT); ... ledcAttach for PWMA/PWMB; STBY high.
#endif
}

void forward() {
    Serial.println("[motor] forward");
#if MOTORS_ENABLED
    // TODO: drive both motors forward at cruising PWM.
#endif
}

void back() {
    Serial.println("[motor] back");
#if MOTORS_ENABLED
    // TODO
#endif
}

void left() {
    Serial.println("[motor] left");
#if MOTORS_ENABLED
    // TODO
#endif
}

void right() {
    Serial.println("[motor] right");
#if MOTORS_ENABLED
    // TODO
#endif
}

void stop() {
    Serial.println("[motor] stop");
#if MOTORS_ENABLED
    // TODO: set both motor PWMs to 0; pull dir pins low.
#endif
}

}  // namespace motor_driver

// Target: ESP32-S3-CAM (brain)
// OV2640 capture init. Detection logic is intentionally stubbed — Phase 2
// per ROBOT_FIRMWARE_PLAN.md §9.

#include "vision.h"

#include <Arduino.h>

#if defined(PETBOT_ENABLE_STREAM) && PETBOT_ENABLE_STREAM
  #include "esp_camera.h"

  // AI-Thinker-style routing carried over from the original ESP32-CAM
  // firmware. If your S3-CAM variant uses a different pin map, override
  // these defines in build_flags or edit here.
  #ifndef CAM_PWDN
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
  #endif
#endif

namespace vision {

static unsigned long g_last_seen_ms = 0;

void init() {
#if defined(PETBOT_ENABLE_STREAM) && PETBOT_ENABLE_STREAM
    camera_config_t cfg = {};
    cfg.ledc_channel = LEDC_CHANNEL_0; cfg.ledc_timer = LEDC_TIMER_0;
    cfg.pin_d0 = CAM_D0; cfg.pin_d1 = CAM_D1; cfg.pin_d2 = CAM_D2;
    cfg.pin_d3 = CAM_D3; cfg.pin_d4 = CAM_D4; cfg.pin_d5 = CAM_D5;
    cfg.pin_d6 = CAM_D6; cfg.pin_d7 = CAM_D7;
    cfg.pin_xclk = CAM_XCLK; cfg.pin_pclk = CAM_PCLK;
    cfg.pin_vsync = CAM_VSYNC; cfg.pin_href = CAM_HREF;
    cfg.pin_sscb_sda = CAM_SIOD; cfg.pin_sscb_scl = CAM_SIOC;
    cfg.pin_pwdn = CAM_PWDN; cfg.pin_reset = CAM_RESET;
    cfg.xclk_freq_hz = 20000000; cfg.pixel_format = PIXFORMAT_JPEG;
    cfg.frame_size = FRAMESIZE_QVGA; cfg.jpeg_quality = 10; cfg.fb_count = 2;
    if (esp_camera_init(&cfg) != ESP_OK) Serial.println("[vision] OV2640 init FAILED");
    else                                 Serial.println("[vision] OV2640 ready");
#else
    Serial.println("[vision] disabled (no stream/ML build)");
#endif
}

void update() {
    // TODO: pull a frame, run a tiny detector, update g_last_seen_ms.
}

bool          targetSeen()           { return false; }
int           targetX()              { return 0; }
int           targetSize()           { return 0; }
unsigned long millisSinceLastSeen()  { return millis() - g_last_seen_ms; }

}  // namespace vision

// Target: ESP32-S3-CAM (brain)
// Static menu tree + button-driven navigation. Selecting a leaf calls into
// app_state, which is the same path the BLE/web command dispatcher takes.

#include "menu_controller.h"

#include <Arduino.h>
#include <string.h>

#include "app_state.h"
#include "protocol/frame.h"
#include "protocol/packets.h"
#include "transport/transport.h"

namespace menu_controller {

struct Menu;
struct MenuItem {
    const char* label;
    void      (*action)();      // null when this descends into `submenu`
    const Menu* submenu;        // null when this is a leaf
};

struct Menu {
    const char*     title;
    const MenuItem* items;
    uint8_t         n_items;
    const Menu*     parent;
};

// ─── Leaf actions ────────────────────────────────────────────────────────────
static void act_face_idle()    { app_state::setExpression(FaceMode::IDLE); }
static void act_face_happy()   { app_state::setExpression(FaceMode::HAPPY); }
static void act_face_search()  { app_state::setExpression(FaceMode::SEARCH); }
static void act_face_curious() { app_state::setExpression(FaceMode::CURIOUS); }
static void act_face_sleep()   { app_state::setExpression(FaceMode::SLEEP); }

static void act_mode_manual()  { app_state::setMode(CtrlMode::MANUAL); }
static void act_mode_auto()    { app_state::setMode(CtrlMode::AUTO); }

static void act_drive_fwd()    { app_state::moveForward(); }
static void act_drive_back()   { app_state::moveBack(); }
static void act_drive_left()   { app_state::moveLeft(); }
static void act_drive_right()  { app_state::moveRight(); }
static void act_drive_stop()   { app_state::moveStop(); }

// ─── Menu tree ───────────────────────────────────────────────────────────────
static const MenuItem kFacesItems[] = {
    { "Idle",    act_face_idle,    nullptr },
    { "Happy",   act_face_happy,   nullptr },
    { "Search",  act_face_search,  nullptr },
    { "Curious", act_face_curious, nullptr },
    { "Sleep",   act_face_sleep,   nullptr },
};
static const MenuItem kModesItems[] = {
    { "Manual",  act_mode_manual,  nullptr },
    { "Auto",    act_mode_auto,    nullptr },
};
static const MenuItem kDriveItems[] = {
    { "Forward", act_drive_fwd,    nullptr },
    { "Back",    act_drive_back,   nullptr },
    { "Left",    act_drive_left,   nullptr },
    { "Right",   act_drive_right,  nullptr },
    { "Stop",    act_drive_stop,   nullptr },
};

static const Menu kRoot;
static const Menu kFaces = { "Faces", kFacesItems, sizeof(kFacesItems)/sizeof(kFacesItems[0]), &kRoot };
static const Menu kModes = { "Modes", kModesItems, sizeof(kModesItems)/sizeof(kModesItems[0]), &kRoot };
static const Menu kDrive = { "Drive", kDriveItems, sizeof(kDriveItems)/sizeof(kDriveItems[0]), &kRoot };

static const MenuItem kRootItems[] = {
    { "Modes", nullptr, &kModes },
    { "Faces", nullptr, &kFaces },
    { "Drive", nullptr, &kDrive },
};
static const Menu kRoot = { "PetBot", kRootItems, sizeof(kRootItems)/sizeof(kRootItems[0]), nullptr };

// ─── Navigation state ────────────────────────────────────────────────────────
static const Menu* s_current  = &kRoot;
static uint8_t     s_selected = 0;
static uint8_t     s_seq      = 0;
static bool        s_c6_ready = false;

// ─── Wire helpers ────────────────────────────────────────────────────────────
static void send_set_menu() {
    if (!s_c6_ready) return;
    uint8_t buf[PB_MAX_PAYLOAD];
    size_t  off = 0;

    buf[off++] = s_selected;

    size_t title_len = strnlen(s_current->title, 200);
    if (off + 1 + title_len + 1 > sizeof(buf)) return;
    buf[off++] = (uint8_t)title_len;
    memcpy(&buf[off], s_current->title, title_len);
    off += title_len;

    buf[off++] = s_current->n_items;
    for (uint8_t i = 0; i < s_current->n_items; ++i) {
        size_t l = strnlen(s_current->items[i].label, 200);
        if (off + 1 + l > sizeof(buf)) return;
        buf[off++] = (uint8_t)l;
        memcpy(&buf[off], s_current->items[i].label, l);
        off += l;
    }

    uint8_t enc[PB_MAX_FRAME];
    size_t  n = pb_encode(enc, sizeof(enc), PB_SET_MENU, s_seq++, buf, (uint16_t)off);
    if (n > 0) transport().write(enc, n);
}

void pushDebugLine(const char* text) {
    if (!s_c6_ready || !text) return;
    uint8_t buf[PB_MAX_PAYLOAD];
    size_t  text_len = strnlen(text, 200);
    if (7 + text_len > sizeof(buf)) text_len = sizeof(buf) - 7;
    // x=8 y=140 color=0xFFE0 (yellow) size=1
    buf[0] = 0; buf[1] = 8;
    buf[2] = 0; buf[3] = 140;
    buf[4] = 0xFF; buf[5] = 0xE0;
    buf[6] = 1;
    memcpy(&buf[7], text, text_len);

    uint8_t enc[PB_MAX_FRAME];
    size_t  n = pb_encode(enc, sizeof(enc), PB_DRAW_TEXT, s_seq++, buf,
                          (uint16_t)(7 + text_len));
    if (n > 0) transport().write(enc, n);
}

// ─── Public API ──────────────────────────────────────────────────────────────
void init() {
    s_current  = &kRoot;
    s_selected = 0;
    s_c6_ready = false;
}

void update() { /* nothing periodic yet */ }

void onHelloFromC6() {
    s_c6_ready = true;
    Serial.println("[menu] C6 said HELLO — pushing root menu");
    s_current  = &kRoot;
    s_selected = 0;
    send_set_menu();
}

void onStateChanged() {
    // Today the menu doesn't show live state; future expansion can render
    // "Mode: auto" / "Face: HAPPY" in the title bar from here.
    send_set_menu();
}

// One-button mapping (see ROBOT_FIRMWARE_PLAN.md §9 — only BOOT is wired
// today). Short press = move selection down; long press = select the
// current item; release = no-op.
//
// Once dedicated UP/DOWN/SELECT/BACK buttons are wired (PB_BTN_UP etc.)
// route them here directly.
void onButtonEvent(uint8_t btn_id, uint8_t edge) {
    if (btn_id == PB_BTN_BOOT) {
        if (edge == PB_BTN_PRESS) {
            s_selected = (uint8_t)((s_selected + 1) % s_current->n_items);
            send_set_menu();
        } else if (edge == PB_BTN_LONGPRESS) {
            const MenuItem& it = s_current->items[s_selected];
            if (it.action) {
                Serial.printf("[menu] activate '%s'\n", it.label);
                it.action();
            } else if (it.submenu) {
                Serial.printf("[menu] descend into '%s'\n", it.label);
                s_current  = it.submenu;
                s_selected = 0;
                send_set_menu();
            }
        }
        return;
    }

    // Dedicated nav buttons (when wired).
    switch (btn_id) {
        case PB_BTN_UP:
            if (edge != PB_BTN_PRESS) return;
            s_selected = (uint8_t)((s_selected + s_current->n_items - 1) % s_current->n_items);
            send_set_menu();
            break;
        case PB_BTN_DOWN:
            if (edge != PB_BTN_PRESS) return;
            s_selected = (uint8_t)((s_selected + 1) % s_current->n_items);
            send_set_menu();
            break;
        case PB_BTN_SELECT: {
            if (edge != PB_BTN_PRESS) return;
            const MenuItem& it = s_current->items[s_selected];
            if (it.action) it.action();
            else if (it.submenu) { s_current = it.submenu; s_selected = 0; send_set_menu(); }
            break;
        }
        case PB_BTN_BACK:
            if (edge != PB_BTN_PRESS) return;
            if (s_current->parent) {
                s_current = s_current->parent;
                s_selected = 0;
                send_set_menu();
            }
            break;
    }
}

}  // namespace menu_controller

// Target: ESP32-S3-CAM (brain)
// UART implementation. Uses HardwareSerial1 by convention; pins and baud
// come from the constructor so the transport selector later in this sketch owns the policy choice.

#include "transport_uart.h"

TransportUart::TransportUart(HardwareSerial& port, int rx_pin, int tx_pin,
                             uint32_t baud)
    : port_(port), rx_pin_(rx_pin), tx_pin_(tx_pin), baud_(baud) {}

bool TransportUart::begin() {
    port_.begin(baud_, SERIAL_8N1, rx_pin_, tx_pin_);
    started_ = true;
    return true;
}

size_t TransportUart::write(const uint8_t* data, size_t len) {
    return started_ ? port_.write(data, len) : 0;
}

int TransportUart::read() {
    if (!started_ || port_.available() == 0) return -1;
    return port_.read();
}

size_t TransportUart::available() {
    return started_ ? port_.available() : 0;
}

// Target: ESP32-S3-CAM (brain)
//
// TODO: USB CDC HOST transport — STUBBED until the UART path is fully working.
//
// On the S3 side this class will eventually drive a TinyUSB host stack and
// wrap the USBHostSerial class shipped in Arduino-ESP32 v3.x. It is NOT
// implemented yet because bring-up requires hardware verification that
// can't be done blind:
//
//   1. Confirm the specific S3-CAM carrier exposes a NATIVE USB port (the
//      D+/D- pins of the ESP32-S3 itself), in addition to any CH340/CP2102
//      UART-only port used for flashing. Some S3-CAM clones only expose
//      the UART bridge — those cannot be USB hosts.
//   2. Confirm the USB-C receptacle is wired for HOST mode: CC1 / CC2 each
//      pulled to GND through 5.1 kΩ. Without that, a USB-C cable plugged
//      into a device-mode peripheral will not negotiate.
//   3. Confirm the board can drive +5 V outward on VBUS. If it can't, a
//      USB-C-to-USB-A-host OTG adapter plus an A-to-C cable into the C6 is
//      required.
//
// If any of those is no, document it in HARDWARE.md and either keep using
// UART transport or fit the OTG adapter.
//
// When implementation begins:
//   - Switch the build env to enable TinyUSB host (`board_build.f_cpu`,
//     `build_flags = -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=0`,
//     plus the TinyUSB host include path).
//   - In begin():   USB.begin(); host_serial_.begin();
//   - In write():   forward bytes to host_serial_.write().
//   - In read():    pull from host_serial_ when host_serial_.available().
//   - In available(): host_serial_.available().
//
// Re-run BRINGUP.md from step 1 after switching transport. Until any of
// that is in place, begin() returns false so the firmware can fail fast
// rather than silently dropping frames.

#include "transport_usbcdc.h"

bool TransportUsbCdc::begin() {
    return false;
}

// Target: ESP32-S3-CAM (brain)
// Build-flag-selected transport singleton. Choose UART or USB-CDC at
// compile time; setup()/loop() call transport().begin() / read() / write().

#include "transport.h"

#if defined(PB_TRANSPORT_USBCDC) && PB_TRANSPORT_USBCDC
  #include "transport_usbcdc.h"
  static TransportUsbCdc g_transport;
#else
  // UART defaults for the S3-CAM ↔ C6 link.
  // RX = GPIO18, TX = GPIO17 (per ROBOT_FIRMWARE_PLAN.md §7).
  // Adjust pin numbers if your S3-CAM carrier conflicts.
  #include "transport_uart.h"
  static TransportUart g_transport(Serial1, /*rx*/18, /*tx*/17, 921600);
#endif

Transport& transport() { return g_transport; }

// Target: ESP32-S3-CAM (brain)
// Ported from the original esp32_cam_brain.ino. Owns BLE NUS, the WiFi
// captive portal, the embedded HTML web UI, and the MJPEG stream handler.
// The command dispatcher now resolves all verbs through app_state so the
// menu controller and the phone share one code path.

#include "ble_web.h"

#include "app_state.h"
#include "menu_controller.h"

#ifndef PETBOT_ENABLE_STREAM
#define PETBOT_ENABLE_STREAM 0
#endif
#ifndef PETBOT_ENABLE_WIFI
// WiFi needs the "Huge APP (3 MB No OTA)" partition — see huge_app.csv.
#define PETBOT_ENABLE_WIFI PETBOT_ENABLE_STREAM
#endif

#if PETBOT_ENABLE_WIFI
  #include <WiFi.h>
  #include <WiFiManager.h>
  #include <ESPmDNS.h>
  #include "esp_http_server.h"
#endif
#if PETBOT_ENABLE_STREAM
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
        String s = String("STATUS:ok,face=") + app_state::expressionName(app_state::expression())
                 + ",mode=" + app_state::modeName(app_state::mode());
      #if PETBOT_ENABLE_WIFI
        s += ",web=http://" MDNS_NAME ".local";
      #endif
        send(s);
    } else {
        send("ERR:unknown:" + cmd);
    }
}

// ─── WiFi + web UI ───────────────────────────────────────────────────────────
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

static httpd_handle_t s_httpd = nullptr;

static esp_err_t handle_root(httpd_req_t* r) {
    httpd_resp_set_type(r, "text/html");
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

  #if PETBOT_ENABLE_STREAM
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

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&s_httpd, &cfg) == ESP_OK) {
        httpd_uri_t ru = { "/",    HTTP_GET, handle_root, nullptr };
        httpd_uri_t cu = { "/cmd", HTTP_GET, handle_cmd,  nullptr };
        httpd_register_uri_handler(s_httpd, &ru);
        httpd_register_uri_handler(s_httpd, &cu);
      #if PETBOT_ENABLE_STREAM
        httpd_uri_t su = { "/stream", HTTP_GET, handle_stream, nullptr };
        httpd_register_uri_handler(s_httpd, &su);
      #endif
        Serial.println("[HTTP] Web UI active");
    }
}

#else  // !PETBOT_ENABLE_WIFI
static void setup_wifi() {
    Serial.println("[WiFi] disabled — compile with -DPETBOT_ENABLE_WIFI=1 + Huge APP partition");
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

// Target: ESP32-S3-CAM (brain)
// Slim entry point. setup() boots each subsystem in dependency order;
// loop() pulls bytes from the C6 transport, dispatches frames, and ticks
// the periodic update() hooks. All command surfaces (BLE, web, C6 menu)
// resolve through app_state — no module owns its own copy of state.

#include <Arduino.h>

#include "app_state.h"
#include "audio_player.h"
#include "ble_web.h"
#include "face_render.h"
#include "menu_controller.h"
#include "motor_driver.h"
#include "vision.h"
#include "protocol/frame.h"
#include "protocol/packets.h"
#include "transport/transport.h"

static uint8_t      s_decoder_buf[PB_MAX_PAYLOAD];
static pb_decoder_t s_decoder;

static void on_frame(const pb_frame_t& f) {
    switch (f.type) {
        case PB_HELLO:
            menu_controller::onHelloFromC6();
            break;
        case PB_BTN_EVENT:
            if (f.len >= 2) menu_controller::onButtonEvent(f.payload[0], f.payload[1]);
            break;
        case PB_LOG: {
            uint8_t lvl = (f.len >= 1) ? f.payload[0] : 0;
            Serial.printf("[c6][lvl=%u] ", lvl);
            for (uint16_t i = 1; i < f.len; ++i) Serial.write((char)f.payload[i]);
            Serial.println();
            break;
        }
        case PB_ACK:
        case PB_NAK:
            // Reserved for future flow control — log and ignore for now.
            break;
        default:
            Serial.printf("[s3] unknown C6 packet 0x%02X len=%u\n", f.type, (unsigned)f.len);
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== PetBot S3 brain booting ===");

    app_state::init();
    motor_driver::init();
    audio_player::init();
    face_render::init();
    vision::init();

    if (!transport().begin()) {
        Serial.println("[transport] begin() FAILED — check PB_TRANSPORT_* build flags");
    }
    pb_decoder_init(&s_decoder, s_decoder_buf, sizeof(s_decoder_buf));

    menu_controller::init();
    ble_web::init();

    Serial.println("=== PetBot S3 brain ready ===");
}

void loop() {
    while (transport().available() > 0) {
        int b = transport().read();
        if (b < 0) break;
        pb_frame_t frame{};
        pb_status_t s = pb_feed(&s_decoder, (uint8_t)b, &frame);
        if (s == PB_OK) {
            on_frame(frame);
        } else if (s == PB_ERR_CRC) {
            Serial.println("[s3] frame dropped: CRC mismatch");
        } else if (s == PB_ERR_LEN) {
            Serial.println("[s3] frame dropped: length > buffer cap");
        }
    }

    face_render::update();
    vision::update();
    menu_controller::update();
    delay(2);
}
