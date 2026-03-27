/*
  ESP32-C6 Display Board Firmware (Screen Only)
  ----------------------------------------------
  Purpose:
  - This firmware ONLY handles face rendering on the ST7789 display.
  - It receives high-level face commands over UART from the ESP32-CAM brain.

  UART protocol from ESP32-CAM:
  - FACE,IDLE
  - FACE,HAPPY
  - FACE,SEARCH
  - FACE,CURIOUS
  - FACE,SLEEP
  - BLINK
*/

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// Fixed display pins (reserved for ST7789)
static constexpr int PIN_TFT_MOSI = 6;
static constexpr int PIN_TFT_SCLK = 7;
static constexpr int PIN_TFT_CS   = 14;
static constexpr int PIN_TFT_DC   = 15;
static constexpr int PIN_TFT_RST  = 21;
static constexpr int PIN_TFT_BL   = 22;

// UART to ESP32-CAM brain
static constexpr int PIN_UART_RX = 18; // C6 RX <- CAM TX
static constexpr int PIN_UART_TX = 17; // C6 TX -> CAM RX (optional)
static constexpr uint32_t UART_BAUD = 115200;

static constexpr int SCREEN_W = 172;
static constexpr int SCREEN_H = 320;

Adafruit_ST7789 tft(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

enum class FaceMode : uint8_t {
  IDLE,
  HAPPY,
  SEARCH,
  CURIOUS,
  SLEEP
};

FaceMode currentMode = FaceMode::IDLE;
bool blinkActive = false;
uint32_t blinkUntilMs = 0;
uint32_t nextAutoBlinkMs = 0;

String uartLine;

uint16_t bgForMode(FaceMode mode) {
  switch (mode) {
    case FaceMode::HAPPY: return ST77XX_GREEN;
    case FaceMode::SEARCH: return ST77XX_ORANGE;
    case FaceMode::CURIOUS: return ST77XX_CYAN;
    case FaceMode::SLEEP: return ST77XX_BLUE;
    case FaceMode::IDLE:
    default: return ST77XX_BLACK;
  }
}

void drawEye(int cx, int cy, int w, int h, uint16_t color) {
  tft.fillRoundRect(cx - w / 2, cy - h / 2, w, h, 8, color);
}

void renderFace() {
  tft.fillScreen(bgForMode(currentMode));

  const int leftX = SCREEN_W / 2 - 40;
  const int rightX = SCREEN_W / 2 + 40;
  const int eyeY = SCREEN_H / 2 - 20;

  int eyeW = 30;
  int eyeH = blinkActive ? 4 : 44;

  if (currentMode == FaceMode::SLEEP) {
    eyeH = 4;
  } else if (currentMode == FaceMode::HAPPY) {
    eyeH = blinkActive ? 3 : 28;
  } else if (currentMode == FaceMode::SEARCH) {
    eyeW = 20;
    eyeH = blinkActive ? 3 : 50;
  }

  drawEye(leftX, eyeY, eyeW, eyeH, ST77XX_WHITE);
  drawEye(rightX, eyeY, eyeW, eyeH, ST77XX_WHITE);

  // Simple mouth line for expression hint
  int mouthY = SCREEN_H / 2 + 50;
  if (currentMode == FaceMode::HAPPY) {
    tft.drawFastHLine(SCREEN_W / 2 - 20, mouthY, 40, ST77XX_WHITE);
    tft.drawFastHLine(SCREEN_W / 2 - 18, mouthY + 1, 36, ST77XX_WHITE);
  } else if (currentMode == FaceMode::SEARCH) {
    tft.drawFastHLine(SCREEN_W / 2 - 10, mouthY, 20, ST77XX_WHITE);
  } else if (currentMode == FaceMode::SLEEP) {
    tft.drawFastHLine(SCREEN_W / 2 - 16, mouthY, 32, ST77XX_LIGHTGREY);
  } else {
    tft.drawFastHLine(SCREEN_W / 2 - 14, mouthY, 28, ST77XX_WHITE);
  }
}

FaceMode parseMode(const String& token) {
  if (token == "HAPPY") return FaceMode::HAPPY;
  if (token == "SEARCH") return FaceMode::SEARCH;
  if (token == "CURIOUS") return FaceMode::CURIOUS;
  if (token == "SLEEP") return FaceMode::SLEEP;
  return FaceMode::IDLE;
}

void triggerBlink(uint16_t durationMs = 140) {
  blinkActive = true;
  blinkUntilMs = millis() + durationMs;
  renderFace();
}

void handleUartLine(const String& line) {
  if (line.startsWith("FACE,")) {
    String modeToken = line.substring(5);
    modeToken.trim();
    currentMode = parseMode(modeToken);
    renderFace();
    return;
  }

  if (line == "BLINK") {
    triggerBlink();
  }
}

void pollUart() {
  while (Serial1.available()) {
    char c = static_cast<char>(Serial1.read());
    if (c == '\n') {
      uartLine.trim();
      if (uartLine.length() > 0) {
        handleUartLine(uartLine);
      }
      uartLine = "";
    } else if (c != '\r') {
      uartLine += c;
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

  nextAutoBlinkMs = millis() + 2500;
}

void loop() {
  pollUart();

  const uint32_t now = millis();

  if (blinkActive && now >= blinkUntilMs) {
    blinkActive = false;
    renderFace();
    nextAutoBlinkMs = now + 2500;
  }

  if (!blinkActive && currentMode != FaceMode::SLEEP && now >= nextAutoBlinkMs) {
    triggerBlink(100);
  }
}
