/*
  ESP32-CAM Brain Firmware (vision/behavior/motion + Wi-Fi control)
  ==================================================================
  Board target: ESP32-CAM (classic ESP32 module variants)

  Responsibilities:
  - Own robot behavior state machine.
  - Own movement/audio decisions.
  - Communicate expression updates to ESP32-C6 screen board over UART.
  - Provide a temporary Wi-Fi web API for remote control.

  UART protocol with screen board:
    TX: PING,<id> | FACE,<mode> | BLINK
    RX: READY,ESP32C6_SCREEN | PONG,<id> | ACK,FACE,<mode> | ACK,BLINK | ERR,...
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

#if defined(CONFIG_IDF_TARGET_ESP32C6)
#error "This sketch is for ESP32-CAM (ESP32), not ESP32-C6."
#endif

// ===========================
// UART to screen board (ESP32-C6)
// ===========================
static constexpr int PIN_UART_TX_TO_SCREEN = 12;
static constexpr int PIN_UART_RX_FROM_SCREEN = 13;
static constexpr uint32_t UART_BAUD = 115200;
HardwareSerial ScreenSerial(1);

// ===========================
// Temporary Wi-Fi control
// ===========================
static constexpr const char* AP_SSID = "PETBOT_CTRL";
static constexpr const char* AP_PASS = "petbot123";
WebServer server(80);

// ===========================
// Motor placeholders (change for your board wiring)
// ===========================
static constexpr int PIN_MOTOR_AIN1 = 2;
static constexpr int PIN_MOTOR_AIN2 = 14;
static constexpr int PIN_MOTOR_BIN1 = 15;
static constexpr int PIN_MOTOR_BIN2 = 4;

enum class RobotState : uint8_t { IDLE, CURIOUS, DRIVE, SEARCH, HAPPY, SLEEP };
enum class DriveCmd : uint8_t { STOP, FORWARD, LEFT, RIGHT };

RobotState state = RobotState::IDLE;
DriveCmd manualDrive = DriveCmd::STOP;
bool manualMode = false;

bool targetSeen = false;
int targetX = 0;
int targetSize = 0;

uint32_t stateEnteredMs = 0;
uint32_t lastSeenMs = 0;
uint32_t lastPingMs = 0;
uint32_t pingSeq = 0;
uint32_t lastFacePushMs = 0;
uint32_t lastUartRxMs = 0;

String uartRxLine;
String lastScreenAck = "(none)";
bool screenReady = false;

const char* stateText(RobotState s) {
  switch (s) {
    case RobotState::CURIOUS: return "CURIOUS";
    case RobotState::DRIVE: return "DRIVE";
    case RobotState::SEARCH: return "SEARCH";
    case RobotState::HAPPY: return "HAPPY";
    case RobotState::SLEEP: return "SLEEP";
    default: return "IDLE";
  }
}

const char* faceForState(RobotState s) {
  switch (s) {
    case RobotState::HAPPY: return "HAPPY";
    case RobotState::SEARCH: return "SEARCH";
    case RobotState::CURIOUS:
    case RobotState::DRIVE: return "CURIOUS";
    case RobotState::SLEEP: return "SLEEP";
    default: return "IDLE";
  }
}

void sendToScreen(const String& line) {
  ScreenSerial.println(line);
}

void pushFace() {
  sendToScreen(String("FACE,") + faceForState(state));
}

void stopMotors() {
  digitalWrite(PIN_MOTOR_AIN1, LOW);
  digitalWrite(PIN_MOTOR_AIN2, LOW);
  digitalWrite(PIN_MOTOR_BIN1, LOW);
  digitalWrite(PIN_MOTOR_BIN2, LOW);
}

void driveForward() {
  digitalWrite(PIN_MOTOR_AIN1, HIGH);
  digitalWrite(PIN_MOTOR_AIN2, LOW);
  digitalWrite(PIN_MOTOR_BIN1, HIGH);
  digitalWrite(PIN_MOTOR_BIN2, LOW);
}

void turnLeft() {
  digitalWrite(PIN_MOTOR_AIN1, LOW);
  digitalWrite(PIN_MOTOR_AIN2, HIGH);
  digitalWrite(PIN_MOTOR_BIN1, HIGH);
  digitalWrite(PIN_MOTOR_BIN2, LOW);
}

void turnRight() {
  digitalWrite(PIN_MOTOR_AIN1, HIGH);
  digitalWrite(PIN_MOTOR_AIN2, LOW);
  digitalWrite(PIN_MOTOR_BIN1, LOW);
  digitalWrite(PIN_MOTOR_BIN2, HIGH);
}

void applyDriveCommand(DriveCmd cmd) {
  switch (cmd) {
    case DriveCmd::FORWARD: driveForward(); break;
    case DriveCmd::LEFT: turnLeft(); break;
    case DriveCmd::RIGHT: turnRight(); break;
    case DriveCmd::STOP:
    default: stopMotors(); break;
  }
}

void setState(RobotState next) {
  if (state == next) return;
  state = next;
  stateEnteredMs = millis();
  pushFace();

  if (state == RobotState::HAPPY) {
    sendToScreen("BLINK");
  }
}

void runVisionStep() {
  // Temporary simulator until camera tracker is integrated.
  const uint32_t phase = (millis() / 3500UL) % 5UL;
  if (phase <= 2) {
    targetSeen = true;
    targetX = (phase == 0) ? -18 : ((phase == 1) ? 0 : 18);
    targetSize = (phase == 1) ? 70 : 40;
    lastSeenMs = millis();
  } else {
    targetSeen = false;
    targetX = 0;
    targetSize = 0;
  }
}

void tickBehavior() {
  if (manualMode) {
    applyDriveCommand(manualDrive);
    return;
  }

  const uint32_t now = millis();
  const bool staleTarget = (now - lastSeenMs) > 1200;

  if (targetSeen) {
    if (targetSize >= 65) {
      setState(RobotState::HAPPY);
      applyDriveCommand(DriveCmd::STOP);
      return;
    }

    if (targetX < -10) {
      setState(RobotState::CURIOUS);
      applyDriveCommand(DriveCmd::LEFT);
      return;
    }

    if (targetX > 10) {
      setState(RobotState::CURIOUS);
      applyDriveCommand(DriveCmd::RIGHT);
      return;
    }

    setState(RobotState::DRIVE);
    applyDriveCommand(DriveCmd::FORWARD);
    return;
  }

  if (staleTarget) {
    setState(RobotState::SEARCH);
    applyDriveCommand(DriveCmd::LEFT);

    if ((now - stateEnteredMs) > 9000 && state == RobotState::SEARCH) {
      setState(RobotState::IDLE);
      applyDriveCommand(DriveCmd::STOP);
    }
    return;
  }

  setState(RobotState::IDLE);
  applyDriveCommand(DriveCmd::STOP);
}

void onRoot() {
  server.send(200, "text/plain",
              "ESP32-CAM robot API running. Open /api/status or /api/cmd?mode=manual&move=forward");
}

void onStatus() {
  String json = "{";
  json += "\"state\":\"" + String(stateText(state)) + "\",";
  json += "\"manualMode\":" + String(manualMode ? "true" : "false") + ",";
  json += "\"screenReady\":" + String(screenReady ? "true" : "false") + ",";
  json += "\"lastScreenAck\":\"" + lastScreenAck + "\",";
  json += "\"targetSeen\":" + String(targetSeen ? "true" : "false") + ",";
  json += "\"targetX\":" + String(targetX) + ",";
  json += "\"targetSize\":" + String(targetSize);
  json += "}";
  server.send(200, "application/json", json);
}

DriveCmd parseDrive(const String& move) {
  if (move == "forward") return DriveCmd::FORWARD;
  if (move == "left") return DriveCmd::LEFT;
  if (move == "right") return DriveCmd::RIGHT;
  return DriveCmd::STOP;
}

void onCmd() {
  if (server.hasArg("mode")) {
    const String mode = server.arg("mode");
    if (mode == "manual") {
      manualMode = true;
      setState(RobotState::CURIOUS);
    } else if (mode == "auto") {
      manualMode = false;
    }
  }

  if (server.hasArg("move")) {
    manualDrive = parseDrive(server.arg("move"));
  }

  if (server.hasArg("face")) {
    String f = server.arg("face");
    f.toUpperCase();
    sendToScreen(String("FACE,") + f);
  }

  if (server.hasArg("blink")) {
    sendToScreen("BLINK");
  }

  onStatus();
}

void setupWeb() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  server.on("/", HTTP_GET, onRoot);
  server.on("/api/status", HTTP_GET, onStatus);
  server.on("/api/cmd", HTTP_GET, onCmd);
  server.begin();
}

void handleScreenRxLine(const String& line) {
  lastUartRxMs = millis();

  if (line.startsWith("READY,")) {
    screenReady = true;
    lastScreenAck = line;
    pushFace();
    return;
  }

  if (line.startsWith("PONG,")) {
    screenReady = true;
    lastScreenAck = line;
    return;
  }

  if (line.startsWith("ACK,")) {
    lastScreenAck = line;
    return;
  }

  if (line.startsWith("ERR,")) {
    lastScreenAck = line;
  }
}

void pollScreenRx() {
  while (ScreenSerial.available()) {
    const char c = static_cast<char>(ScreenSerial.read());
    if (c == '\n') {
      uartRxLine.trim();
      if (!uartRxLine.isEmpty()) {
        handleScreenRxLine(uartRxLine);
      }
      uartRxLine = "";
    } else if (c != '\r') {
      uartRxLine += c;
      if (uartRxLine.length() > 120) {
        uartRxLine = "";
      }
    }
  }
}

void setupMotors() {
  pinMode(PIN_MOTOR_AIN1, OUTPUT);
  pinMode(PIN_MOTOR_AIN2, OUTPUT);
  pinMode(PIN_MOTOR_BIN1, OUTPUT);
  pinMode(PIN_MOTOR_BIN2, OUTPUT);
  stopMotors();
}

void setup() {
  Serial.begin(115200);
  ScreenSerial.begin(UART_BAUD, SERIAL_8N1, PIN_UART_RX_FROM_SCREEN, PIN_UART_TX_TO_SCREEN);

  setupMotors();
  setupWeb();

  stateEnteredMs = millis();
  lastSeenMs = millis();
  pushFace();
}

void loop() {
  server.handleClient();
  pollScreenRx();

  if (!manualMode) {
    runVisionStep();
  }
  tickBehavior();

  const uint32_t now = millis();

  if (now - lastPingMs > 1000) {
    sendToScreen(String("PING,") + String(pingSeq++));
    lastPingMs = now;
  }

  if (now - lastFacePushMs > 1200) {
    pushFace();
    lastFacePushMs = now;
  }

  if ((now - lastUartRxMs) > 5000) {
    screenReady = false;
  }

  delay(20);
}
