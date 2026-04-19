/*
  ESP32-CAM Brain Firmware
  ------------------------
  Wiring assumptions (AI-Thinker ESP32-CAM):
    UART to ESP32-C6 display board : TX=GPIO1, RX=GPIO3
    Motor driver (TB6612FNG)       : AIN1=12, AIN2=13, BIN1=14, BIN2=15
                                     PWMA=2,  PWMB=4
    Camera (OV2640)                : built-in AI-Thinker pin map
    BLE                            : built-in (Nordic UART Service)
    WiFi AP (camera stream only)   : SSID=PETBOT_CTRL, pass=petbot123
                                     Camera MJPEG stream at /stream

  BLE command protocol (write to RX characteristic):
    MOVE:fwd | back | left | right | stop
    FACE:HAPPY | IDLE | SEARCH | CURIOUS | SLEEP
    SAY:<text>      forwarded to display board as SPEAK:<text>
    SOUND:BOOT | HAPPY | ALERT
    MODE:manual | auto
    STATUS

  Edit pin map in the constants block below to match your wiring.
*/

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <WiFi.h>
#include <WebServer.h>
#include "esp_camera.h"

// ---------------------------------------------------------------------------
// Pin map — edit here
// ---------------------------------------------------------------------------
static constexpr int PIN_UART_TX_TO_DISPLAY   = 1;
static constexpr int PIN_UART_RX_FROM_DISPLAY = 3;
static constexpr uint32_t UART_BAUD           = 115200;

static constexpr int PIN_MOTOR_AIN1 = 12;
static constexpr int PIN_MOTOR_AIN2 = 13;
static constexpr int PIN_MOTOR_BIN1 = 14;
static constexpr int PIN_MOTOR_BIN2 = 15;
static constexpr int PIN_MOTOR_PWMA = 2;
static constexpr int PIN_MOTOR_PWMB = 4;

// ---------------------------------------------------------------------------
// AI-Thinker ESP32-CAM OV2640 pin map (do not change)
// ---------------------------------------------------------------------------
#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK     0
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27
#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      21
#define CAM_PIN_D2      19
#define CAM_PIN_D1      18
#define CAM_PIN_D0       5
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22

// ---------------------------------------------------------------------------
// WiFi / web server
// ---------------------------------------------------------------------------
static const char* WIFI_SSID = "PETBOT_CTRL";
static const char* WIFI_PASS = "petbot123";
static const char* WIFI_IP   = "192.168.4.1";

WebServer webServer(80);

// ---------------------------------------------------------------------------
// BLE Nordic UART Service UUIDs
// ---------------------------------------------------------------------------
#define BLE_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_CHAR_RX_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // phone writes here
#define BLE_CHAR_TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // ESP32 notifies here

BLECharacteristic* pTxChar = nullptr;
bool bleClientConnected    = false;
uint32_t lastBleCommandMs  = 0;
static constexpr uint32_t MANUAL_TIMEOUT_MS = 5000;

// ---------------------------------------------------------------------------
// Robot state machine
// ---------------------------------------------------------------------------
enum class RobotState : uint8_t { IDLE, CURIOUS, DRIVE, SEARCH, HAPPY, SLEEP };
RobotState state        = RobotState::IDLE;
uint32_t stateEnteredMs = 0;
uint32_t lastSeenMs     = 0;
uint32_t lastFacePushMs = 0;

bool manualMode = false;

bool targetSeen  = false;
int  targetX     = 0;
int  targetSize  = 0;

// ---------------------------------------------------------------------------
// UART helpers
// ---------------------------------------------------------------------------
void sendDisplay(const String& msg) {
  Serial1.println(msg);
}

void pushFaceForState(RobotState s) {
  switch (s) {
    case RobotState::HAPPY:                    sendDisplay("FACE,HAPPY");   break;
    case RobotState::SEARCH:                   sendDisplay("FACE,SEARCH");  break;
    case RobotState::CURIOUS:
    case RobotState::DRIVE:                    sendDisplay("FACE,CURIOUS"); break;
    case RobotState::SLEEP:                    sendDisplay("FACE,SLEEP");   break;
    case RobotState::IDLE: default:            sendDisplay("FACE,IDLE");    break;
  }
}

// ---------------------------------------------------------------------------
// Motor control
// ---------------------------------------------------------------------------
void setMotorRaw(int ain1, int ain2, int bin1, int bin2, int pwmA, int pwmB) {
  digitalWrite(PIN_MOTOR_AIN1, ain1);
  digitalWrite(PIN_MOTOR_AIN2, ain2);
  digitalWrite(PIN_MOTOR_BIN1, bin1);
  digitalWrite(PIN_MOTOR_BIN2, bin2);
  analogWrite(PIN_MOTOR_PWMA, constrain(pwmA, 0, 255));
  analogWrite(PIN_MOTOR_PWMB, constrain(pwmB, 0, 255));
}

void stopMotors()              { setMotorRaw(LOW,  LOW,  LOW,  LOW,  0,     0);     }
void turnLeft(int s = 120)     { setMotorRaw(LOW,  HIGH, HIGH, LOW,  s,     s);     }
void turnRight(int s = 120)    { setMotorRaw(HIGH, LOW,  LOW,  HIGH, s,     s);     }
void driveForward(int s = 140) { setMotorRaw(HIGH, LOW,  HIGH, LOW,  s,     s);     }
void driveBackward(int s = 130){ setMotorRaw(LOW,  HIGH, LOW,  HIGH, s,     s);     }

void setupMotorPins() {
  pinMode(PIN_MOTOR_AIN1, OUTPUT);
  pinMode(PIN_MOTOR_AIN2, OUTPUT);
  pinMode(PIN_MOTOR_BIN1, OUTPUT);
  pinMode(PIN_MOTOR_BIN2, OUTPUT);
  pinMode(PIN_MOTOR_PWMA, OUTPUT);
  pinMode(PIN_MOTOR_PWMB, OUTPUT);
  stopMotors();
}

// ---------------------------------------------------------------------------
// Camera setup & MJPEG stream
// ---------------------------------------------------------------------------
bool setupCamera() {
  camera_config_t cfg;
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer   = LEDC_TIMER_0;
  cfg.pin_d0       = CAM_PIN_D0;
  cfg.pin_d1       = CAM_PIN_D1;
  cfg.pin_d2       = CAM_PIN_D2;
  cfg.pin_d3       = CAM_PIN_D3;
  cfg.pin_d4       = CAM_PIN_D4;
  cfg.pin_d5       = CAM_PIN_D5;
  cfg.pin_d6       = CAM_PIN_D6;
  cfg.pin_d7       = CAM_PIN_D7;
  cfg.pin_xclk     = CAM_PIN_XCLK;
  cfg.pin_pclk     = CAM_PIN_PCLK;
  cfg.pin_vsync    = CAM_PIN_VSYNC;
  cfg.pin_href     = CAM_PIN_HREF;
  cfg.pin_sccb_sda = CAM_PIN_SIOD;
  cfg.pin_sccb_scl = CAM_PIN_SIOC;
  cfg.pin_pwdn     = CAM_PIN_PWDN;
  cfg.pin_reset    = CAM_PIN_RESET;
  cfg.xclk_freq_hz = 20000000;
  cfg.pixel_format = PIXFORMAT_JPEG;
  cfg.frame_size   = FRAMESIZE_QVGA;
  cfg.jpeg_quality = 12;
  cfg.fb_count     = 1;
  cfg.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
  cfg.fb_location  = CAMERA_FB_IN_PSRAM;

  esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }
  return true;
}

// Serve a multipart MJPEG stream at GET /stream
void handleStream() {
  WiFiClient client = webServer.client();
  String boundary   = "frame";

  webServer.sendContent(
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: multipart/x-mixed-replace; boundary=" + boundary + "\r\n\r\n"
  );

  while (client.connected()) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) { delay(10); continue; }

    client.printf(
      "--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
      boundary.c_str(), fb->len
    );
    client.write(fb->buf, fb->len);
    client.print("\r\n");
    esp_camera_fb_return(fb);

    delay(33); // ~30 fps cap
  }
}

void handleStatus() {
  const char* stateStr = "IDLE";
  switch (state) {
    case RobotState::HAPPY:   stateStr = "HAPPY";   break;
    case RobotState::SEARCH:  stateStr = "SEARCH";  break;
    case RobotState::CURIOUS: stateStr = "CURIOUS"; break;
    case RobotState::DRIVE:   stateStr = "DRIVE";   break;
    case RobotState::SLEEP:   stateStr = "SLEEP";   break;
    default: break;
  }
  String body = "{\"state\":\"";
  body += stateStr;
  body += "\",\"mode\":\"";
  body += manualMode ? "manual" : "auto";
  body += "\",\"ble\":";
  body += bleClientConnected ? "true" : "false";
  body += "}";
  webServer.send(200, "application/json", body);
}

void setupWifi() {
  WiFi.softAP(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi AP started — http://");
  Serial.println(WiFi.softAPIP());

  webServer.on("/stream", HTTP_GET, handleStream);
  webServer.on("/status", HTTP_GET, handleStatus);
  webServer.begin();
}

// ---------------------------------------------------------------------------
// BLE command dispatch
// ---------------------------------------------------------------------------
void handleMoveCmd(const String& token) {
  manualMode        = true;
  lastBleCommandMs  = millis();

  if      (token == "fwd")   driveForward();
  else if (token == "back")  driveBackward();
  else if (token == "left")  turnLeft();
  else if (token == "right") turnRight();
  else if (token == "stop")  stopMotors();
}

void handleFaceCmd(const String& token) {
  sendDisplay("FACE," + token);
}

void sendBleStatus() {
  if (!pTxChar || !bleClientConnected) return;
  const char* stateStr = "IDLE";
  switch (state) {
    case RobotState::HAPPY:   stateStr = "HAPPY";   break;
    case RobotState::SEARCH:  stateStr = "SEARCH";  break;
    case RobotState::CURIOUS: stateStr = "CURIOUS"; break;
    case RobotState::DRIVE:   stateStr = "DRIVE";   break;
    case RobotState::SLEEP:   stateStr = "SLEEP";   break;
    default: break;
  }
  String msg = "STATUS:";
  msg += stateStr;
  msg += ",";
  msg += manualMode ? "manual" : "auto";
  msg += ",";
  msg += String(millis() / 1000);

  pTxChar->setValue(msg.c_str());
  pTxChar->notify();
}

void onBleRx(const String& cmd) {
  Serial.println("BLE rx: " + cmd);

  if      (cmd.startsWith("MOVE:"))  handleMoveCmd(cmd.substring(5));
  else if (cmd.startsWith("FACE:"))  handleFaceCmd(cmd.substring(5));
  else if (cmd.startsWith("SAY:"))   sendDisplay("SPEAK:" + cmd.substring(4));
  else if (cmd.startsWith("SOUND:")) sendDisplay("SOUND:" + cmd.substring(6));
  else if (cmd == "MODE:manual")     { manualMode = true; lastBleCommandMs = millis(); }
  else if (cmd == "MODE:auto")       { manualMode = false; }
  else if (cmd == "STATUS")          { sendBleStatus(); }
}

// ---------------------------------------------------------------------------
// BLE callbacks
// ---------------------------------------------------------------------------
class BleServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*)    override { bleClientConnected = true;  }
  void onDisconnect(BLEServer* s) override {
    bleClientConnected = false;
    BLEDevice::startAdvertising();
  }
};

class BleRxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String val = String(c->getValue().c_str());
    val.trim();
    if (val.length() > 0) onBleRx(val);
  }
};

void setupBLE() {
  BLEDevice::init("PetBot");
  BLEServer* pServer = BLEDevice::createServer();
  pServer->setCallbacks(new BleServerCallbacks());

  BLEService* pService = pServer->createService(BLE_SERVICE_UUID);

  // TX characteristic — ESP32 notifies phone
  pTxChar = pService->createCharacteristic(
    BLE_CHAR_TX_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pTxChar->addDescriptor(new BLE2902());

  // RX characteristic — phone writes commands here
  BLECharacteristic* pRxChar = pService->createCharacteristic(
    BLE_CHAR_RX_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  pRxChar->setCallbacks(new BleRxCallbacks());

  pService->start();
  BLEAdvertising* pAdv = BLEDevice::getAdvertising();
  pAdv->addServiceUUID(BLE_SERVICE_UUID);
  pAdv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("BLE advertising as 'PetBot'");
}

// ---------------------------------------------------------------------------
// Vision simulator (replace runVisionStep() with real OV2640 detection)
// ---------------------------------------------------------------------------
void runVisionStep() {
  const uint32_t now   = millis();
  const uint32_t phase = (now / 3000UL) % 4UL;

  if (phase == 0 || phase == 1) {
    targetSeen = true;
    targetX    = (phase == 0) ? -20 : 20;
    targetSize = (phase == 1) ? 65  : 40;
    lastSeenMs = now;
  } else {
    targetSeen = false;
    targetX    = 0;
    targetSize = 0;
  }
}

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------
void setState(RobotState nextState) {
  if (state == nextState) return;
  state          = nextState;
  stateEnteredMs = millis();
  pushFaceForState(state);

  if (state == RobotState::HAPPY) {
    sendDisplay("SOUND:HAPPY");
    sendDisplay("BLINK");
  } else if (state == RobotState::IDLE) {
    sendDisplay("SOUND:BOOT");
  }
}

void tickStateMachine() {
  const uint32_t now         = millis();
  const bool     staleTarget = (now - lastSeenMs) > 1200;

  if (targetSeen) {
    if (targetSize > 60)        { setState(RobotState::HAPPY);   stopMotors();      return; }
    if (abs(targetX) > 10)      { setState(RobotState::CURIOUS); (targetX < 0 ? turnLeft(110) : turnRight(110)); return; }
    setState(RobotState::DRIVE); driveForward(135);
    return;
  }

  if (staleTarget) {
    setState(RobotState::SEARCH);
    turnLeft(90);
    if ((now - stateEnteredMs) > 8000 && state == RobotState::SEARCH) {
      setState(RobotState::IDLE);
      stopMotors();
    }
    return;
  }

  setState(RobotState::IDLE);
  stopMotors();
}

// ---------------------------------------------------------------------------
// Setup & loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial1.begin(UART_BAUD, SERIAL_8N1, PIN_UART_RX_FROM_DISPLAY, PIN_UART_TX_TO_DISPLAY);

  setupMotorPins();
  setupBLE();
  setupWifi();

  if (setupCamera()) {
    Serial.println("Camera ready");
  }

  stateEnteredMs = millis();
  lastSeenMs     = millis();
  pushFaceForState(state);
  sendDisplay("SOUND:BOOT");
  sendDisplay("BLINK");
}

void loop() {
  webServer.handleClient();

  // Return to auto mode if no BLE command for MANUAL_TIMEOUT_MS
  if (manualMode && (millis() - lastBleCommandMs) > MANUAL_TIMEOUT_MS) {
    manualMode = false;
    stopMotors();
  }

  if (!manualMode) {
    runVisionStep();
    tickStateMachine();
  }

  const uint32_t now = millis();

  // Periodic face refresh (keeps display in sync)
  if (!manualMode && now - lastFacePushMs > 1000) {
    pushFaceForState(state);
    lastFacePushMs = now;
  }

  // Periodic BLE status notification
  static uint32_t lastBleStatusMs = 0;
  if (bleClientConnected && now - lastBleStatusMs > 2000) {
    sendBleStatus();
    lastBleStatusMs = now;
  }

  delay(20);
}
