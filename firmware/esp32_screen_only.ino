/*
  ESP32-C6 Display Board Firmware
  ---------------------------------
  Handles face rendering (ST7789) AND audio output (MAX98357A I2S).

  UART protocol from ESP32-CAM brain:
    FACE,<IDLE|HAPPY|SEARCH|CURIOUS|SLEEP>   set face expression
    BLINK                                     trigger blink animation
    SPEAK:<text>                              speak text as tone sequence
    SOUND:<BOOT|HAPPY|ALERT>                  play a named sound

  Wiring:
    ST7789 display (fixed — do not change):
      MOSI=GPIO6  SCLK=GPIO7  CS=GPIO14  DC=GPIO15  RST=GPIO21  BL=GPIO22

    UART from ESP32-CAM:
      RX=GPIO18 (C6 RX ← CAM TX)
      TX=GPIO17 (C6 TX → CAM RX, optional)

    MAX98357A I2S amplifier:
      BCLK=GPIO19   LRC/WS=GPIO20   DIN=GPIO23

    INMP441 I2S microphone (not yet populated — set MIC_ENABLED=1 when wired):
      SCK=GPIO8   WS=GPIO9   SD=GPIO10
*/

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <driver/i2s.h>

// ---------------------------------------------------------------------------
// Display pins (reserved — do not change)
// ---------------------------------------------------------------------------
static constexpr int PIN_TFT_MOSI = 6;
static constexpr int PIN_TFT_SCLK = 7;
static constexpr int PIN_TFT_CS   = 14;
static constexpr int PIN_TFT_DC   = 15;
static constexpr int PIN_TFT_RST  = 21;
static constexpr int PIN_TFT_BL   = 22;

// ---------------------------------------------------------------------------
// UART pins
// ---------------------------------------------------------------------------
static constexpr int      PIN_UART_RX   = 18;
static constexpr int      PIN_UART_TX   = 17;
static constexpr uint32_t UART_BAUD     = 115200;

// ---------------------------------------------------------------------------
// I2S audio (MAX98357A)
// ---------------------------------------------------------------------------
static constexpr int PIN_I2S_BCLK = 19;
static constexpr int PIN_I2S_LRC  = 20;
static constexpr int PIN_I2S_DIN  = 23;

static constexpr i2s_port_t I2S_NUM_OUT = I2S_NUM_0;
static constexpr int        SAMPLE_RATE = 16000;

// ---------------------------------------------------------------------------
// I2S microphone stubs (INMP441) — set to 1 when hardware is wired
// ---------------------------------------------------------------------------
#define MIC_ENABLED 0
#if MIC_ENABLED
static constexpr int        PIN_MIC_SCK  = 8;
static constexpr int        PIN_MIC_WS   = 9;
static constexpr int        PIN_MIC_SD   = 10;
static constexpr i2s_port_t I2S_NUM_MIC  = I2S_NUM_1;
#endif

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------
static constexpr int SCREEN_W = 172;
static constexpr int SCREEN_H = 320;

Adafruit_ST7789 tft(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

// ---------------------------------------------------------------------------
// Face state
// ---------------------------------------------------------------------------
enum class FaceMode : uint8_t { IDLE, HAPPY, SEARCH, CURIOUS, SLEEP };
FaceMode currentMode    = FaceMode::IDLE;
bool     blinkActive    = false;
uint32_t blinkUntilMs   = 0;
uint32_t nextAutoBlinkMs = 0;
String   uartLine;

// ---------------------------------------------------------------------------
// I2S audio helpers
// ---------------------------------------------------------------------------
void setupI2SAudio() {
  i2s_config_t cfg = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate          = SAMPLE_RATE,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 8,
    .dma_buf_len          = 64,
    .use_apll             = false,
    .tx_desc_auto_clear   = true,
  };

  i2s_pin_config_t pins = {
    .bck_io_num   = PIN_I2S_BCLK,
    .ws_io_num    = PIN_I2S_LRC,
    .data_out_num = PIN_I2S_DIN,
    .data_in_num  = I2S_PIN_NO_CHANGE,
  };

  i2s_driver_install(I2S_NUM_OUT, &cfg, 0, nullptr);
  i2s_set_pin(I2S_NUM_OUT, &pins);
  i2s_zero_dma_buffer(I2S_NUM_OUT);
}

// Play a pure sine-wave tone at `freq` Hz for `durationMs` milliseconds.
void playTone(int freq, int durationMs) {
  if (freq <= 0 || durationMs <= 0) {
    delay(durationMs);
    return;
  }

  const int   samples   = (SAMPLE_RATE * durationMs) / 1000;
  const float step      = 2.0f * PI * freq / SAMPLE_RATE;
  const int   amplitude = 6000; // safe volume; raise to 20000 for louder

  static int16_t buf[256];
  int written = 0;

  while (written < samples) {
    int chunk = min(256, samples - written);
    for (int i = 0; i < chunk; i++) {
      buf[i] = (int16_t)(amplitude * sinf(step * (written + i)));
    }
    size_t bytes_out = 0;
    i2s_write(I2S_NUM_OUT, buf, chunk * sizeof(int16_t), &bytes_out, portMAX_DELAY);
    written += chunk;
  }
}

// Silence for `ms` milliseconds.
void audioSilence(int ms) {
  playTone(0, ms);
}

// Boot chirp: ascending 3-note arpeggio.
void playBootSound() {
  playTone(523, 80);  // C5
  audioSilence(20);
  playTone(659, 80);  // E5
  audioSilence(20);
  playTone(784, 120); // G5
}

// Happy melody: quick 5-note phrase.
void playHappySound() {
  int freqs[] = { 784, 880, 988, 880, 1047 };
  for (int f : freqs) {
    playTone(f, 70);
    audioSilence(15);
  }
}

// Alert beep: two short pulses.
void playAlertSound() {
  playTone(1200, 60);
  audioSilence(40);
  playTone(1200, 60);
}

// Map a character to a frequency for tone TTS.
static int charToFreq(char c) {
  c = tolower(c);
  if (c >= 'a' && c <= 'z') return 200 + (c - 'a') * 15; // 200–575 Hz
  if (c >= '0' && c <= '9') return 600 + (c - '0') * 20; // 600–780 Hz
  return 0; // space / punctuation = silence
}

// Speak a string as a sequence of short tones (robotic voice effect).
void speakText(const String& text) {
  for (int i = 0; i < (int)text.length() && i < 64; i++) {
    int freq = charToFreq(text[i]);
    if (freq > 0) {
      playTone(freq, 75);
      audioSilence(20);
    } else {
      audioSilence(80); // word gap
    }
  }
}

// ---------------------------------------------------------------------------
// I2S mic stubs — enable with MIC_ENABLED = 1
// ---------------------------------------------------------------------------
#if MIC_ENABLED
void setupI2SMic() {
  i2s_config_t cfg = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate          = SAMPLE_RATE,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 4,
    .dma_buf_len          = 64,
  };
  i2s_pin_config_t pins = {
    .bck_io_num   = PIN_MIC_SCK,
    .ws_io_num    = PIN_MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = PIN_MIC_SD,
  };
  i2s_driver_install(I2S_NUM_MIC, &cfg, 0, nullptr);
  i2s_set_pin(I2S_NUM_MIC, &pins);
}

// Returns mic amplitude 0–100 (call from loop at intervals).
int getMicLevel() {
  static int32_t samples[64];
  size_t bytes_read = 0;
  i2s_read(I2S_NUM_MIC, samples, sizeof(samples), &bytes_read, 10);
  int count = bytes_read / sizeof(int32_t);
  if (count == 0) return 0;
  int64_t sum = 0;
  for (int i = 0; i < count; i++) sum += abs(samples[i] >> 14);
  return (int)constrain(sum / count, 0, 100);
}
#endif

// ---------------------------------------------------------------------------
// Face rendering
// ---------------------------------------------------------------------------
uint16_t bgForMode(FaceMode mode) {
  switch (mode) {
    case FaceMode::HAPPY:   return ST77XX_GREEN;
    case FaceMode::SEARCH:  return ST77XX_ORANGE;
    case FaceMode::CURIOUS: return ST77XX_CYAN;
    case FaceMode::SLEEP:   return ST77XX_BLUE;
    default:                return ST77XX_BLACK;
  }
}

void drawEye(int cx, int cy, int w, int h, uint16_t color) {
  tft.fillRoundRect(cx - w / 2, cy - h / 2, w, h, 8, color);
}

/*
  renderFace() — customise your bot's face here.

  Screen coords (after setRotation(1)):
    Width  = SCREEN_W = 172 px  (horizontal)
    Height = SCREEN_H = 320 px  (vertical)

  Tips for drawing a custom face:
    - Eyes:     drawEye(cx, cy, width, height, color)
                cx/cy is the centre; use SCREEN_W/2 ± offset for left/right
    - Pupils:   tft.fillCircle(cx, cy, radius, ST77XX_BLACK)
    - Eyebrows: tft.fillRoundRect(x, y, w, h, r, color) above each eye
    - Mouth:    tft.drawLine / tft.fillTriangle / tft.drawCircle
    - Blink:    when blinkActive == true, collapse eyeH to ≤4 px
    - Per-mode: use a switch(currentMode) inside this function for
                expression-specific shapes (e.g. frown when SEARCH)
*/
void renderFace() {
  tft.fillScreen(bgForMode(currentMode));

  const int leftX  = SCREEN_W / 2 - 40;
  const int rightX = SCREEN_W / 2 + 40;
  const int eyeY   = SCREEN_H / 2 - 20;

  int eyeW = 30;
  int eyeH = blinkActive ? 4 : 44;

  if      (currentMode == FaceMode::SLEEP)   { eyeH = 4; }
  else if (currentMode == FaceMode::HAPPY)   { eyeH = blinkActive ? 3 : 28; }
  else if (currentMode == FaceMode::SEARCH)  { eyeW = 20; eyeH = blinkActive ? 3 : 50; }

  drawEye(leftX,  eyeY, eyeW, eyeH, ST77XX_WHITE);
  drawEye(rightX, eyeY, eyeW, eyeH, ST77XX_WHITE);

  // Pupils (skipped during blink)
  if (!blinkActive && currentMode != FaceMode::SLEEP) {
    tft.fillCircle(leftX,  eyeY, 8, ST77XX_BLACK);
    tft.fillCircle(rightX, eyeY, 8, ST77XX_BLACK);
  }

  // Mouth
  const int mouthY = SCREEN_H / 2 + 50;
  switch (currentMode) {
    case FaceMode::HAPPY:
      tft.drawFastHLine(SCREEN_W / 2 - 20, mouthY,     40, ST77XX_WHITE);
      tft.drawFastHLine(SCREEN_W / 2 - 18, mouthY + 1, 36, ST77XX_WHITE);
      break;
    case FaceMode::SEARCH:
      tft.drawFastHLine(SCREEN_W / 2 - 10, mouthY, 20, ST77XX_WHITE);
      break;
    case FaceMode::SLEEP:
      tft.drawFastHLine(SCREEN_W / 2 - 16, mouthY, 32, ST77XX_LIGHTGREY);
      break;
    default:
      tft.drawFastHLine(SCREEN_W / 2 - 14, mouthY, 28, ST77XX_WHITE);
      break;
  }
}

// ---------------------------------------------------------------------------
// UART command parser
// ---------------------------------------------------------------------------
FaceMode parseMode(const String& token) {
  if (token == "HAPPY")   return FaceMode::HAPPY;
  if (token == "SEARCH")  return FaceMode::SEARCH;
  if (token == "CURIOUS") return FaceMode::CURIOUS;
  if (token == "SLEEP")   return FaceMode::SLEEP;
  return FaceMode::IDLE;
}

void triggerBlink(uint16_t durationMs = 140) {
  blinkActive  = true;
  blinkUntilMs = millis() + durationMs;
  renderFace();
}

void handleUartLine(const String& line) {
  if (line.startsWith("FACE,")) {
    String tok = line.substring(5);
    tok.trim();
    currentMode = parseMode(tok);
    renderFace();
    return;
  }

  if (line == "BLINK") {
    triggerBlink();
    return;
  }

  if (line.startsWith("SPEAK:")) {
    String text = line.substring(6);
    text.trim();
    speakText(text);
    return;
  }

  if (line.startsWith("SOUND:")) {
    String name = line.substring(6);
    name.trim();
    if      (name == "HAPPY") playHappySound();
    else if (name == "ALERT") playAlertSound();
    else                      playBootSound();
    return;
  }
}

void pollUart() {
  while (Serial1.available()) {
    char c = static_cast<char>(Serial1.read());
    if (c == '\n') {
      uartLine.trim();
      if (uartLine.length() > 0) handleUartLine(uartLine);
      uartLine = "";
    } else if (c != '\r') {
      uartLine += c;
    }
  }
}

// ---------------------------------------------------------------------------
// Setup & loop
// ---------------------------------------------------------------------------
void setup() {
  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, HIGH);

  Serial.begin(115200);
  Serial1.begin(UART_BAUD, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);

  SPI.begin(PIN_TFT_SCLK, -1, PIN_TFT_MOSI, PIN_TFT_CS);
  tft.init(SCREEN_W, SCREEN_H);
  tft.setRotation(1);

  setupI2SAudio();
#if MIC_ENABLED
  setupI2SMic();
#endif

  currentMode = FaceMode::IDLE;
  renderFace();
  playBootSound();

  nextAutoBlinkMs = millis() + 2500;
}

void loop() {
  pollUart();

  const uint32_t now = millis();

  if (blinkActive && now >= blinkUntilMs) {
    blinkActive     = false;
    renderFace();
    nextAutoBlinkMs = now + 2500;
  }

  if (!blinkActive && currentMode != FaceMode::SLEEP && now >= nextAutoBlinkMs) {
    triggerBlink(100);
  }
}
