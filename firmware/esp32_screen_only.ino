/*
  ESP32-C6 Display Board Firmware (Screen + UART Bridge Only)
  ============================================================
  Board target: ESP32-C6 display board with ST7789 (172x320)

  Responsibilities:
  - Render robot face and expressions.
  - Receive expression commands from ESP32-CAM over UART.
  - Acknowledge commands so the CAM side can confirm link health.

  UART protocol (line-based, '\n' terminated):
    CAM -> SCREEN:
      PING,<id>
      FACE,<IDLE|HAPPY|SEARCH|CURIOUS|SLEEP>
      BLINK

    SCREEN -> CAM:
      READY,ESP32C6_SCREEN
      PONG,<id>
      ACK,FACE,<mode>
      ACK,BLINK
      ERR,<reason>
*/

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#if !defined(CONFIG_IDF_TARGET_ESP32C6)
#warning "This sketch is intended for ESP32-C6 display boards."
#endif

// Fixed display pins provided by your ESP32-C6 display board wiring.
static constexpr int PIN_TFT_MOSI = 6;
static constexpr int PIN_TFT_SCLK = 7;
static constexpr int PIN_TFT_CS   = 14;
static constexpr int PIN_TFT_DC   = 15;
static constexpr int PIN_TFT_RST  = 21;
static constexpr int PIN_TFT_BL   = 22;

// UART to ESP32-CAM
static constexpr int PIN_UART_RX = 18; // C6 RX <- CAM TX
static constexpr int PIN_UART_TX = 17; // C6 TX -> CAM RX
static constexpr uint32_t UART_BAUD = 115200;

static constexpr int SCREEN_W = 172;
static constexpr int SCREEN_H = 320;

Adafruit_ST7789 tft(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

enum class FaceMode : uint8_t { IDLE, HAPPY, SEARCH, CURIOUS, SLEEP };

FaceMode currentMode = FaceMode::IDLE;
String rxLine;
bool blinkActive = false;
uint32_t blinkUntilMs = 0;
uint32_t nextAutoBlinkMs = 0;

void sendUart(const String& msg) {
  Serial1.println(msg);
}

const char* modeToText(FaceMode mode) {
  switch (mode) {
    case FaceMode::HAPPY: return "HAPPY";
    case FaceMode::SEARCH: return "SEARCH";
    case FaceMode::CURIOUS: return "CURIOUS";
    case FaceMode::SLEEP: return "SLEEP";
    default: return "IDLE";
  }
}

FaceMode textToMode(String token) {
  token.trim();
  token.toUpperCase();
  if (token == "HAPPY") return FaceMode::HAPPY;
  if (token == "SEARCH") return FaceMode::SEARCH;
  if (token == "CURIOUS") return FaceMode::CURIOUS;
  if (token == "SLEEP") return FaceMode::SLEEP;
  return FaceMode::IDLE;
}

uint16_t bgColor(FaceMode mode) {
  switch (mode) {
    case FaceMode::HAPPY: return ST77XX_GREEN;
    case FaceMode::SEARCH: return ST77XX_ORANGE;
    case FaceMode::CURIOUS: return ST77XX_CYAN;
    case FaceMode::SLEEP: return ST77XX_BLUE;
    default: return ST77XX_BLACK;
  }
}

void drawEye(int cx, int cy, int w, int h, uint16_t color) {
  tft.fillRoundRect(cx - w / 2, cy - h / 2, w, h, 8, color);
}

void renderFace() {
  tft.fillScreen(bgColor(currentMode));

  const int leftX = (SCREEN_W / 2) - 40;
  const int rightX = (SCREEN_W / 2) + 40;
  const int eyeY = (SCREEN_H / 2) - 24;

  int eyeW = 30;
  int eyeH = blinkActive ? 4 : 44;

  if (currentMode == FaceMode::SLEEP) {
    eyeH = 4;
  } else if (currentMode == FaceMode::HAPPY) {
    eyeH = blinkActive ? 3 : 28;
  } else if (currentMode == FaceMode::SEARCH) {
    eyeW = 20;
    eyeH = blinkActive ? 3 : 52;
  }

  drawEye(leftX, eyeY, eyeW, eyeH, ST77XX_WHITE);
  drawEye(rightX, eyeY, eyeW, eyeH, ST77XX_WHITE);

  const int mouthY = (SCREEN_H / 2) + 50;
  if (currentMode == FaceMode::HAPPY) {
    tft.drawFastHLine((SCREEN_W / 2) - 22, mouthY, 44, ST77XX_WHITE);
  } else if (currentMode == FaceMode::SEARCH) {
    tft.drawFastHLine((SCREEN_W / 2) - 8, mouthY, 16, ST77XX_WHITE);
  } else if (currentMode == FaceMode::SLEEP) {
    tft.drawFastHLine((SCREEN_W / 2) - 16, mouthY, 32, ST77XX_LIGHTGREY);
  } else {
    tft.drawFastHLine((SCREEN_W / 2) - 14, mouthY, 28, ST77XX_WHITE);
  }
}

void triggerBlink(uint16_t ms = 110) {
  blinkActive = true;
  blinkUntilMs = millis() + ms;
  renderFace();
}

void handleLine(const String& line) {
  if (line.startsWith("PING,")) {
    sendUart(String("PONG,") + line.substring(5));
    return;
  }

  if (line.startsWith("FACE,")) {
    String modeToken = line.substring(5);
    currentMode = textToMode(modeToken);
    renderFace();
    sendUart(String("ACK,FACE,") + modeToText(currentMode));
    return;
  }

  if (line == "BLINK") {
    triggerBlink();
    sendUart("ACK,BLINK");
    return;
  }

  sendUart("ERR,UNKNOWN_CMD");
}

void pollUart() {
  while (Serial1.available()) {
    const char c = static_cast<char>(Serial1.read());

    if (c == '\n') {
      rxLine.trim();
      if (!rxLine.isEmpty()) {
        handleLine(rxLine);
      }
      rxLine = "";
    } else if (c != '\r') {
      rxLine += c;
      if (rxLine.length() > 120) {
        rxLine = "";
        sendUart("ERR,LINE_TOO_LONG");
      }
    }
  }
}

void setup() {
  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, HIGH);

  Serial.begin(115200);
  Serial1.begin(UART_BAUD, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);

  SPI.begin(PIN_TFT_SCLK, -1, PIN_TFT_MOSI, PIN_TFT_CS);
  tft.init(SCREEN_W, SCREEN_H);
  tft.setRotation(1);

  currentMode = FaceMode::IDLE;
  renderFace();
  nextAutoBlinkMs = millis() + 2200;

  sendUart("READY,ESP32C6_SCREEN");
}

void loop() {
  pollUart();

  const uint32_t now = millis();
  if (blinkActive && now >= blinkUntilMs) {
    blinkActive = false;
    renderFace();
    nextAutoBlinkMs = now + 2200;
  }

  if (!blinkActive && currentMode != FaceMode::SLEEP && now >= nextAutoBlinkMs) {
    triggerBlink(100);
  }
}
