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
