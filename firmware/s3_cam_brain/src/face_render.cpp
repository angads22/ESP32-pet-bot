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
