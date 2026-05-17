// Target: ESP32-S3-CAM (brain)
// OV2640 capture init. Detection logic is intentionally stubbed — Phase 2
// per ROBOT_FIRMWARE_PLAN.md §9. The camera comes up whenever STREAM is
// enabled OR AP-mode is on (the AP-mode app expects MJPEG at /stream).

#include "vision.h"

#include <Arduino.h>

#if (defined(PETBOT_ENABLE_STREAM) && PETBOT_ENABLE_STREAM) || \
    (defined(PETBOT_AP_MODE)        && PETBOT_AP_MODE)
  #define PETBOT_NEED_CAMERA 1
#endif

#if PETBOT_NEED_CAMERA
  #include "esp_camera.h"

  // ── Camera pin map ──────────────────────────────────────────────────────
  // Default below is the Freenove ESP32-S3 WROOM CAM board (the user's
  // confirmed carrier — see HARDWARE_MAP.md). The legacy AI-Thinker ESP32-
  // CAM pin map is retained behind PETBOT_BOARD_AITHINKER for posterity.
  #if defined(PETBOT_BOARD_AITHINKER) && PETBOT_BOARD_AITHINKER
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
  #else
    // Freenove ESP32-S3 WROOM CAM (default)
    #define CAM_PWDN   -1
    #define CAM_RESET  -1
    #define CAM_XCLK   15
    #define CAM_SIOD    4
    #define CAM_SIOC    5
    #define CAM_D7     16
    #define CAM_D6     17
    #define CAM_D5     18
    #define CAM_D4     12
    #define CAM_D3     10
    #define CAM_D2      8
    #define CAM_D1      9
    #define CAM_D0     11
    #define CAM_VSYNC   6
    #define CAM_HREF    7
    #define CAM_PCLK   13
  #endif
#endif

namespace vision {

static unsigned long g_last_seen_ms = 0;

void init() {
#if PETBOT_NEED_CAMERA
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
    Serial.println("[vision] disabled (no AP-mode / no stream build)");
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
