/*
 *  PetBot / Marvin — main body sketch
 *  ───────────────────────────────────
 *  Board     : Freenove ESP32-WROVER CAM (classic ESP32, OV2640/OV3660)
 *  Drives    : 12 leg servos via PCA9685, optional touch sensor, MJPEG
 *              camera stream, joystick gait engine.
 *
 *  ── Features ─────────────────────────────────────────────────────────
 *    - Open WiFi AP "PetBot_xxxx" / pw petbot123 by default; switch to
 *      "join home WiFi" from Settings tab → reachable at petbot.local.
 *    - Tabbed web UI (Move / Face / Calibrate / Settings).
 *    - Joystick → 50 Hz trot-gait task with smooth interpolation.
 *      Joystick magnitude < 0.7 = WALK pace, > 0.7 = RUN pace.
 *    - Live MJPEG camera stream at /stream.
 *    - PCA9685 servo driver, NVS-backed home pose, "Save Home" button.
 *    - Touch sensor input (TTP223 module on GPIO 15) → tells the C6
 *      head to show LOVE face.
 *    - Auto FACE:WALK / RUN / IDLE / LOVE printed to Serial as state
 *      changes (sent to C6 head over the wired UART link when set up).
 *
 *  ── Required libraries ───────────────────────────────────────────────
 *    - Adafruit PWM Servo Driver Library    by Adafruit
 *
 *  ── Tools menu (classic Freenove ESP32-WROVER CAM) ───────────────────
 *    Board                  : AI Thinker ESP32-CAM
 *    Flash Mode             : QIO
 *    Partition Scheme       : Huge APP (3MB No OTA/1MB SPIFFS)
 *    PSRAM                  : Enabled
 *    Upload Speed           : 921600
 *
 *  ── Wiring ───────────────────────────────────────────────────────────
 *    PCA9685 SDA      → ESP32-CAM GPIO 13
 *    PCA9685 SCL      → ESP32-CAM GPIO 14
 *    PCA9685 VCC      → 3.3V
 *    PCA9685 V+       → 5–6 V REGULATED supply (NOT raw battery!)
 *    GND tied between battery, PCA9685, and ESP32-CAM.
 *
 *    Touch sensor (TTP223 module OUT) → GPIO 15 (idle LOW, press HIGH)
 *
 *  ── Servo channels (this robot's confirmed wiring) ───────────────────
 *    Front-Left   hip=3  thigh=1  calf=2
 *    Front-Right  hip=15 thigh=14 calf=13
 *    Back-Left    hip=7  thigh=6  calf=5
 *    Back-Right   hip=8  thigh=9  calf=10
 *
 *  ── Reset WiFi config ────────────────────────────────────────────────
 *    Power off → hold BOOT (GPIO 0) → power on → keep holding 3 s →
 *    wipes saved WiFi credentials, next boot is AP mode.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "esp_http_server.h"
#include "esp_camera.h"
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <math.h>

// ─── Pin map ────────────────────────────────────────────────────────────
// Camera (classic ESP32-CAM / Freenove WROVER CAM)
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

// I2C → PCA9685
#define I2C_SDA       13
#define I2C_SCL       14
#define PCA9685_ADDR  0x40

// Touch input. Default is GPIO 15 for a TTP223 module's OUT pin
// (digital, idle LOW, press HIGH). GPIO 15 is a strap pin but is read
// after boot completes, so a sensor that idles LOW is safe.
// If your sensor module idles HIGH (some variants), prefer GPIO 12, or
// disable by setting -1.
#define TOUCH_PIN  15

// Onboard LEDs on the Freenove WROVER CAM. Both forced OFF at boot; you
// can toggle them from the Move tab or via /led endpoint at runtime.
//   FLASH  = white front LED on GPIO 4   (active HIGH — HIGH = on)
//   STATUS = red status LED on GPIO 33   (active LOW  — LOW  = on)
// Set either to -1 to leave the pin uninitialised.
#define LED_FLASH_PIN   4
#define LED_STATUS_PIN  33

// ─── Servo layout ───────────────────────────────────────────────────────
// PCA9685 channel → physical leg+joint mapping for THIS robot.
// Grouped FL, FR, BL, BR with [hip, thigh, calf] inside each leg.
const uint8_t LEG_CH[12] = {
     3,  1,  2,   // FL: hip=ch3,  thigh=ch1,  calf=ch2
    15, 14, 13,   // FR: hip=ch15, thigh=ch14, calf=ch13
     7,  6,  5,   // BL: hip=ch7,  thigh=ch6,  calf=ch5
     8,  9, 10,   // BR: hip=ch8,  thigh=ch9,  calf=ch10
};
const char* LEG_NAME[12] = {
    "FL hip", "FL thigh", "FL calf",
    "FR hip", "FR thigh", "FR calf",
    "BL hip", "BL thigh", "BL calf",
    "BR hip", "BR thigh", "BR calf",
};

// Calibrated home pose (overridable via Save Home in the app).
static const uint16_t HOME_US[12] = {
    /* FL hip   ch  3 */ 1496,
    /* FL thigh ch  1 */ 1734,
    /* FL calf  ch  2 */ 1347,
    /* FR hip   ch 15 */ 1509,
    /* FR thigh ch 14 */ 906,
    /* FR calf  ch 13 */ 1517,
    /* BL hip   ch  7 */ 1709,
    /* BL thigh ch  6 */ 1711,
    /* BL calf  ch  5 */ 1384,
    /* BR hip   ch  8 */ 1426,
    /* BR thigh ch  9 */ 1119,
    /* BR calf  ch 10 */ 1605,
};

// Per-leg motion direction signs. Derived from the calibration mirror:
// left thighs sit ABOVE 1500 µs, right thighs sit BELOW → to lift a leg,
// left pulse decreases and right pulse increases. Hip "forward" sign is
// a guess (clockwise from above on both sides); flip if motion is wrong.
struct LegSign { int8_t hip_fwd; int8_t thigh_lift; int8_t calf_flex; };
static const LegSign LEG_SIGN[4] = {
    /* FL */ { +1, -1, +1 },
    /* FR */ { -1, +1, -1 },
    /* BL */ { +1, -1, +1 },
    /* BR */ { -1, +1, -1 },
};

// Gait travel limits (per joint, µs delta from home).
#define HIP_SWING_RANGE   140
#define THIGH_LIFT_RANGE  200
#define CALF_FLEX_RANGE   170
#define STEP_HZ_WALK      1.5f
#define STEP_HZ_RUN       3.0f

// ─── Globals ────────────────────────────────────────────────────────────
static Adafruit_PWMServoDriver s_pca(PCA9685_ADDR);
static Preferences             s_prefs;
static uint16_t s_servo_us[16]   = {0};
static uint16_t s_home_us[16]    = {0};
static int16_t  s_current_us[16] = {0};   // for smoothing
static bool     s_pca_ok         = false;
static bool     s_camera_ok      = false;
static httpd_handle_t s_httpd    = nullptr;
static char     s_ap_ssid[32]    = {0};
static String   s_home_ssid      = "";
static String   s_home_pass      = "";
static uint8_t  s_wifi_mode      = 0;
static String   s_active_ip      = "";
static String   s_active_mode    = "AP";

// Gait
struct GaitCmd {
    volatile float x, y, speed;
    volatile bool  run_mode;
    volatile bool  active;
};
static GaitCmd s_gait_cmd = {0, 0, 0, false, false};
static volatile bool s_gait_request_home = false;

// Touch
static bool     s_touch_pressed     = false;
static uint32_t s_touch_press_at    = 0;

// LED state (true = lit)
static bool     s_led_flash_on  = false;
static bool     s_led_status_on = false;

// Auto-FACE state (printed to Serial as it changes)
static const char* s_face_state = "IDLE";

// ─── Servo helpers ──────────────────────────────────────────────────────
static inline uint16_t us_to_ticks(uint16_t us) {
    return (uint32_t)us * 4096UL / 20000UL;
}

static void servo_set_us(uint8_t ch, uint16_t us) {
    if (us < 500)  us = 500;
    if (us > 2500) us = 2500;
    s_servo_us[ch] = us;
    if (s_pca_ok) s_pca.setPWM(ch, 0, us_to_ticks(us));
}

static void servo_release(uint8_t ch) {
    if (s_pca_ok) s_pca.setPWM(ch, 0, 4096);
}

static void all_release() {
    for (uint8_t i = 0; i < 12; i++) servo_release(LEG_CH[i]);
}

// Smooth interpolation (called every gait tick).
static void servo_set_us_smooth(uint8_t ch, uint16_t target, float alpha = 0.35f) {
    int16_t cur = s_current_us[ch];
    int16_t step = (int16_t)((int16_t)target - cur);
    int16_t newv = cur + (int16_t)(step * alpha);
    s_current_us[ch] = newv;
    servo_set_us(ch, (uint16_t)newv);
}

// ─── NVS ────────────────────────────────────────────────────────────────
static uint16_t home_default_for(uint8_t ch) {
    for (uint8_t i = 0; i < 12; i++) if (LEG_CH[i] == ch) return HOME_US[i];
    return 1500;
}

static void load_home() {
    s_prefs.begin("home", true);
    for (uint8_t ch = 0; ch < 16; ch++) {
        char k[6]; snprintf(k, sizeof(k), "c%u", ch);
        s_home_us[ch]    = s_prefs.getUShort(k, home_default_for(ch));
        s_servo_us[ch]   = s_home_us[ch];
        s_current_us[ch] = s_home_us[ch];
    }
    s_prefs.end();
}

static void save_home() {
    s_prefs.begin("home", false);
    for (uint8_t ch = 0; ch < 16; ch++) {
        char k[6]; snprintf(k, sizeof(k), "c%u", ch);
        s_home_us[ch] = s_servo_us[ch];
        s_prefs.putUShort(k, s_home_us[ch]);
    }
    s_prefs.end();
}

static void load_wifi_cfg() {
    s_prefs.begin("wifi", true);
    s_wifi_mode = s_prefs.getUChar("mode", 0);
    s_home_ssid = s_prefs.getString("ssid", "");
    s_home_pass = s_prefs.getString("pass", "");
    s_prefs.end();
}

static void save_wifi_cfg() {
    s_prefs.begin("wifi", false);
    s_prefs.putUChar("mode", s_wifi_mode);
    s_prefs.putString("ssid", s_home_ssid);
    s_prefs.putString("pass", s_home_pass);
    s_prefs.end();
}

static void clear_wifi_cfg() {
    s_prefs.begin("wifi", false);
    s_prefs.clear();
    s_prefs.end();
}

// ─── Auto-FACE ──────────────────────────────────────────────────────────
static void update_face_state(const char* name) {
    if (s_face_state == name) return;
    s_face_state = name;
    Serial.print("FACE:"); Serial.println(name);
    // Serial2.print("FACE:"); Serial2.println(name);  // when wired bot→C6
}

// ─── Gait engine ────────────────────────────────────────────────────────
// Trot: FL+BR airborne while FR+BL plant, swap diagonally each half-cycle.
static void apply_gait_pose(float phase, float turn_x, float speed_y) {
    for (int leg = 0; leg < 4; leg++) {
        bool diag_a = (leg == 0 || leg == 3);          // FL, BR
        float p = phase;
        if (!diag_a) p = fmodf(phase + 0.5f, 1.0f);

        // Lift profile: half-cycle sine, peaks at p=0.25, zero at p=0/0.5
        float lift = (p < 0.5f) ? sinf(p * 2.0f * (float)M_PI) : 0.0f;
        if (lift < 0) lift = 0;

        // Hip swing: -1 (rear) at p=0, +1 (forward) at p=0.5, back to -1
        float swing = (p < 0.5f) ? (-1.0f + (p / 0.5f) * 2.0f)
                                 : ( 1.0f - ((p - 0.5f) / 0.5f) * 2.0f);

        // Direction: forward (+y) keeps swing as-is; backward flips it
        float dir_sign = (speed_y >= 0) ? 1.0f : -1.0f;
        float swing_amt = swing * dir_sign;
        // Magnitude tracks |speed_y| with a floor so very slow walks still move
        float spd_mag = fminf(1.0f, fabsf(speed_y) + 0.2f);
        swing_amt *= spd_mag;

        // Turn: left turn (turn_x < 0) pushes right legs forward, left back.
        // Right turn does the opposite. Asymmetry steers in place.
        if (leg == 0 || leg == 2) {  // FL, BL  (left side)
            swing_amt += turn_x * 0.6f * fabsf(swing);
        } else {                     // FR, BR  (right side)
            swing_amt -= turn_x * 0.6f * fabsf(swing);
        }
        if (swing_amt >  1.0f) swing_amt =  1.0f;
        if (swing_amt < -1.0f) swing_amt = -1.0f;

        int hip_d   = (int)(swing_amt * HIP_SWING_RANGE);
        int thigh_d = (int)(lift      * THIGH_LIFT_RANGE);
        int calf_d  = (int)(lift      * CALF_FLEX_RANGE);

        uint8_t hip_ch   = LEG_CH[leg * 3 + 0];
        uint8_t thigh_ch = LEG_CH[leg * 3 + 1];
        uint8_t calf_ch  = LEG_CH[leg * 3 + 2];

        int hip_target   = (int)s_home_us[hip_ch]   + LEG_SIGN[leg].hip_fwd    * hip_d;
        int thigh_target = (int)s_home_us[thigh_ch] + LEG_SIGN[leg].thigh_lift * thigh_d;
        int calf_target  = (int)s_home_us[calf_ch]  + LEG_SIGN[leg].calf_flex  * calf_d;

        servo_set_us_smooth(hip_ch,   (uint16_t)hip_target);
        servo_set_us_smooth(thigh_ch, (uint16_t)thigh_target);
        servo_set_us_smooth(calf_ch,  (uint16_t)calf_target);
    }
}

static void apply_home_smooth() {
    for (uint8_t i = 0; i < 12; i++) {
        servo_set_us_smooth(LEG_CH[i], s_home_us[LEG_CH[i]], 0.18f);
    }
}

static void gait_task_fn(void* /*arg*/) {
    for (uint8_t i = 0; i < 16; i++) s_current_us[i] = s_servo_us[i];
    float phase = 0;
    while (true) {
        if (s_gait_request_home) {
            apply_home_smooth();
            update_face_state("IDLE");
            bool arrived = true;
            for (uint8_t i = 0; i < 12 && arrived; i++) {
                if (abs(s_current_us[LEG_CH[i]] - (int)s_home_us[LEG_CH[i]]) > 4) arrived = false;
            }
            if (arrived) s_gait_request_home = false;
        } else if (s_gait_cmd.active && s_gait_cmd.speed > 0.1f) {
            float hz = s_gait_cmd.run_mode ? STEP_HZ_RUN : STEP_HZ_WALK;
            hz *= fminf(1.0f, s_gait_cmd.speed * 0.7f + 0.5f);
            phase += hz * 0.02f;
            if (phase >= 1.0f) phase -= 1.0f;
            apply_gait_pose(phase, s_gait_cmd.x, s_gait_cmd.y);
            update_face_state(s_gait_cmd.run_mode ? "RUN" : "WALK");
        } else if (s_touch_pressed && (millis() - s_touch_press_at) < 3000) {
            update_face_state("LOVE");
        } else {
            update_face_state("IDLE");
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ─── LEDs ───────────────────────────────────────────────────────────────
static void led_flash(bool on) {
    if (LED_FLASH_PIN < 0) return;
    digitalWrite(LED_FLASH_PIN, on ? HIGH : LOW);   // active HIGH
    s_led_flash_on = on;
}
static void led_status(bool on) {
    if (LED_STATUS_PIN < 0) return;
    digitalWrite(LED_STATUS_PIN, on ? LOW : HIGH);  // active LOW
    s_led_status_on = on;
}
static void leds_init() {
    if (LED_FLASH_PIN >= 0) {
        pinMode(LED_FLASH_PIN, OUTPUT);
        digitalWrite(LED_FLASH_PIN, LOW);          // OFF
    }
    if (LED_STATUS_PIN >= 0) {
        pinMode(LED_STATUS_PIN, OUTPUT);
        digitalWrite(LED_STATUS_PIN, HIGH);        // OFF (active low)
    }
}

// ─── Touch sensor ───────────────────────────────────────────────────────
static void poll_touch() {
    if (TOUCH_PIN < 0) return;
    bool now = digitalRead(TOUCH_PIN) == HIGH;
    if (now && !s_touch_pressed) {
        s_touch_press_at = millis();
        Serial.println("[touch] press");
    }
    s_touch_pressed = now;
}

// ─── Web app (single page, professional dark theme) ────────────────────
static const char WEBAPP_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Marvin</title>
<style>
:root{
  --bg-0:#0b0f17;--bg-1:#141a26;--bg-2:#1c2433;--line:rgba(255,255,255,.06);
  --text-0:#e7ecf3;--text-1:#8d97ab;
  --accent:#5b8def;--accent-soft:rgba(91,141,239,.12);
  --good:#4ade80;--warn:#fbbf24;--bad:#f87171;
  --radius:10px;--shadow:0 4px 14px rgba(0,0,0,.35);
}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Inter,sans-serif}
body{background:var(--bg-0);color:var(--text-0);margin:0 auto;padding:14px;max-width:560px;font-size:14px;line-height:1.5}
header{display:flex;align-items:center;justify-content:space-between;padding:4px 0 16px;margin-bottom:14px;border-bottom:1px solid var(--line)}
.brand{display:flex;align-items:center;gap:10px;font-weight:600;font-size:17px;letter-spacing:.01em}
.brand-dot{width:8px;height:8px;border-radius:50%;background:var(--good);box-shadow:0 0 10px var(--good)}
.status{display:flex;align-items:center;gap:6px;font:11px ui-monospace,monospace;color:var(--text-1);padding:6px 10px;background:var(--bg-1);border:1px solid var(--line);border-radius:99px;max-width:260px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.status .dot{width:6px;height:6px;border-radius:50%;background:var(--text-1);flex-shrink:0}
.status.on .dot{background:var(--good)}
nav.tabs{display:flex;gap:3px;background:var(--bg-1);border-radius:var(--radius);padding:4px;margin-bottom:16px;border:1px solid var(--line)}
nav.tabs button{flex:1;padding:9px;background:transparent;color:var(--text-1);border:none;font-size:13px;font-weight:500;cursor:pointer;border-radius:7px;transition:all .15s;font-family:inherit}
nav.tabs button.active{background:var(--bg-2);color:var(--text-0);box-shadow:0 2px 6px rgba(0,0,0,.25)}
.tab{display:none;animation:fade .2s ease-out}
.tab.active{display:block}
@keyframes fade{from{opacity:0;transform:translateY(4px)}to{opacity:1;transform:none}}
.card{background:var(--bg-1);border:1px solid var(--line);border-radius:var(--radius);padding:14px;margin-bottom:12px}
.card-title{font-size:11px;text-transform:uppercase;letter-spacing:.08em;color:var(--text-1);margin:0 0 12px;font-weight:600}
.cam-wrap{position:relative;border-radius:var(--radius);overflow:hidden;background:#000;aspect-ratio:4/3;border:1px solid var(--line)}
#cam{width:100%;display:block}
#cam-off{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;color:var(--text-1);font-size:13px;flex-direction:column;gap:8px}
#cam-off .icon{font-size:32px;opacity:.4}
.joystick{margin:12px auto 0;width:200px;height:200px;border-radius:50%;background:radial-gradient(circle at 50% 50%,var(--bg-2) 30%,var(--bg-1) 100%);border:1px solid var(--line);position:relative;touch-action:none;box-shadow:inset 0 4px 8px rgba(0,0,0,.4)}
.joystick::before{content:'';position:absolute;inset:30px;border-radius:50%;border:1px dashed rgba(255,255,255,.06)}
.nub{position:absolute;top:50%;left:50%;width:62px;height:62px;margin:-31px 0 0 -31px;border-radius:50%;background:radial-gradient(circle at 40% 35%,#7aa5f5,#3d6cc4);transition:transform .08s cubic-bezier(.2,1,.4,1);pointer-events:none;box-shadow:0 4px 12px rgba(0,0,0,.4),inset 0 -2px 4px rgba(0,0,0,.2)}
.gauge{margin:14px 4px 0}
.gauge .label{display:flex;justify-content:space-between;color:var(--text-1);font-size:11px;margin-bottom:5px;text-transform:uppercase;letter-spacing:.06em;font-weight:500}
.gauge progress{width:100%;height:5px;-webkit-appearance:none;appearance:none;background:var(--bg-2);border-radius:3px;border:none;overflow:hidden}
.gauge progress::-webkit-progress-bar{background:var(--bg-2);border-radius:3px}
.gauge progress::-webkit-progress-value{background:linear-gradient(90deg,var(--accent),#7aa5f5);border-radius:3px;transition:width .15s}
.actions{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:8px}
.btn{padding:10px 14px;background:var(--bg-2);color:var(--text-0);border:1px solid var(--line);border-radius:8px;font-size:13px;font-weight:500;cursor:pointer;transition:all .12s;font-family:inherit}
.btn:hover{background:var(--accent-soft);border-color:rgba(91,141,239,.4)}
.btn:active{transform:scale(.97)}
.btn.primary{background:var(--accent);border-color:var(--accent);color:#fff}
.btn.primary:hover{background:#4a7ce0}
.btn.danger{background:transparent;border-color:var(--bad);color:var(--bad)}
.btn.danger:hover{background:rgba(248,113,113,.1)}
.row{display:flex;align-items:center;gap:8px;margin:6px 0;padding:8px 10px;background:var(--bg-2);border-radius:6px;border:1px solid var(--line)}
.row .ch{font:600 11px ui-monospace,monospace;color:var(--accent);flex:0 0 38px}
.row .joint{font-size:12px;color:var(--text-1);flex:0 0 48px}
.row input[type=range]{flex:1;accent-color:var(--accent);min-width:0;height:18px}
.row .us{font:11px ui-monospace,monospace;color:var(--text-0);min-width:44px;text-align:right}
.row .row-btn{padding:5px 9px;background:var(--bg-1);color:var(--text-0);border:1px solid var(--line);border-radius:5px;font-size:11px;cursor:pointer;font-family:inherit}
.row .row-btn:active{background:var(--accent);color:#fff}
.leg-group{padding:12px}
.group-title{font-size:11px;color:var(--accent);text-transform:uppercase;letter-spacing:.08em;font-weight:600;margin:0 0 8px;padding-left:2px}
input[type=text],input[type=password]{width:100%;padding:10px 12px;background:var(--bg-2);color:var(--text-0);border:1px solid var(--line);border-radius:6px;font-size:14px;font-family:inherit;margin-top:6px}
input:focus{outline:none;border-color:var(--accent);box-shadow:0 0 0 3px var(--accent-soft)}
.radio{display:flex;align-items:center;gap:10px;padding:12px;background:var(--bg-2);border:1px solid var(--line);border-radius:6px;margin:6px 0;cursor:pointer;font-size:13px;transition:all .12s}
.radio:hover{border-color:rgba(91,141,239,.4)}
.radio.checked{border-color:var(--accent);background:var(--accent-soft)}
.radio input{accent-color:var(--accent);margin:0}
.radio b{font-weight:600}
.radio em{font-style:normal;color:var(--text-1);font-size:12px;display:block;margin-top:2px}
.hint{font-size:12px;color:var(--text-1);line-height:1.55;margin:0}
.hint code{background:var(--bg-2);padding:1px 5px;border-radius:3px;font-size:11px}
.face-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(110px,1fr));gap:6px}
.face-grid .btn{font-size:12px;padding:9px 6px;text-align:center}
.toast{position:fixed;bottom:20px;left:50%;transform:translateX(-50%) translateY(8px);background:var(--bg-2);color:var(--text-0);padding:10px 16px;border-radius:8px;border:1px solid var(--accent);box-shadow:var(--shadow);font-size:13px;opacity:0;transition:all .2s;pointer-events:none;z-index:100;max-width:90%;text-align:center}
.toast.show{opacity:1;transform:translateX(-50%) translateY(0)}
</style></head><body>

<header>
  <div class="brand"><div class="brand-dot"></div>Marvin</div>
  <div class="status" id="status"><div class="dot"></div><span>connecting…</span></div>
</header>

<nav class="tabs">
  <button data-tab="move" class="active">Move</button>
  <button data-tab="face">Face</button>
  <button data-tab="calib">Calibrate</button>
  <button data-tab="set">Settings</button>
</nav>

<section id="tab-move" class="tab active">
  <div class="card" style="padding:6px">
    <div class="cam-wrap">
      <img id="cam" src="/stream" onerror="camOff()">
      <div id="cam-off" style="display:none">
        <div class="icon">⌖</div>
        <div>camera offline</div>
      </div>
    </div>
  </div>
  <div class="card">
    <div class="card-title">Joystick</div>
    <div class="joystick" id="jb"><div class="nub" id="jn"></div></div>
    <div class="gauge">
      <div class="label"><span id="mode-label">idle</span><span id="speed-label">0%</span></div>
      <progress id="speed-bar" value="0" max="100"></progress>
    </div>
  </div>
  <div class="actions">
    <button class="btn primary" onclick="c('HOME')">Stand at home</button>
    <button class="btn danger" onclick="stop()">Stop</button>
  </div>
  <div class="card">
    <div class="card-title">Onboard LEDs</div>
    <div class="actions">
      <button class="btn" id="btn-flash" onclick="ledToggle('flash')">💡 Flash LED · off</button>
      <button class="btn" id="btn-status" onclick="ledToggle('status')">🔴 Status LED · off</button>
    </div>
  </div>
</section>

<section id="tab-face" class="tab">
  <div class="card">
    <p class="hint" style="margin-bottom:12px">Pick a face. Marvin sends it to the C6 head display (over the wired UART link once you connect it).</p>
    <div class="face-grid">
      <button class="btn" onclick="f('IDLE')">(·_·) idle</button>
      <button class="btn" onclick="f('SLEEP')">(=_=) sleep</button>
      <button class="btn" onclick="f('COOL')">(⌐■_■) cool</button>
      <button class="btn" onclick="f('WINK')">(^_~) wink</button>
      <button class="btn" onclick="f('HAPPY')">(^ω^) happy</button>
      <button class="btn" onclick="f('EXCITED')">(★ω★) excited</button>
      <button class="btn" onclick="f('LOVE')">(♡_♡) love</button>
      <button class="btn" onclick="f('CURIOUS')">(?_?) curious</button>
      <button class="btn" onclick="f('SAD')">(︶︹︶) sad</button>
      <button class="btn" onclick="f('CRY')">(T_T) cry</button>
      <button class="btn" onclick="f('ANGRY')">(ಠ益ಠ) angry</button>
      <button class="btn" onclick="f('EMBARRASSED')">(//ω//) shy</button>
      <button class="btn" onclick="f('DIZZY')">(@_@) dizzy</button>
      <button class="btn" onclick="f('SURPRISED')">(⊙_⊙) wow</button>
      <button class="btn" onclick="f('SEARCH')">(•_•) search</button>
      <button class="btn" onclick="f('TABLE_FLIP')">┻━┻ flip</button>
      <button class="btn" onclick="f('WALK')">walk anim</button>
      <button class="btn" onclick="f('RUN')">run anim</button>
      <button class="btn" onclick="f('CONTENT')">(─‿‿─) content</button>
      <button class="btn" onclick="f('CHILL')">(¬‿¬) chill</button>
      <button class="btn" onclick="f('BEAR')">ʕ•ᴥ•ʔ bear</button>
      <button class="btn" onclick="f('PEEK')">(◕‿◕) peek</button>
      <button class="btn" onclick="f('MISCHIEF')">(ಠ‿ಠ) mischief</button>
    </div>
  </div>
</section>

<section id="tab-calib" class="tab">
  <div class="card">
    <p class="hint">Slide each joint until Marvin stands cleanly. Tap <b>R</b> to release a servo so you can move it by hand. <b>Save Home</b> persists to NVS — the dog will return to this pose on every reboot.</p>
  </div>
  <div id="legs"></div>
  <div class="actions">
    <button class="btn primary" onclick="saveHome()">Save Home</button>
    <button class="btn" onclick="releaseAll()">Release All</button>
    <button class="btn" onclick="goHome()">Reset to saved</button>
  </div>
</section>

<section id="tab-set" class="tab">
  <div class="card">
    <div class="card-title">WiFi mode</div>
    <p class="hint" style="margin-bottom:10px">Choose how you reach Marvin. <b>AP</b> broadcasts <code>PetBot_xxxx</code>; <b>Home</b> joins your existing network at <code>petbot.local</code>.</p>
    <label class="radio" id="r-ap"><input type="radio" name="wm" value="0"><span><b>AP mode</b><em>broadcast its own WiFi network</em></span></label>
    <label class="radio" id="r-sta"><input type="radio" name="wm" value="1"><span><b>Home WiFi</b><em>join an existing network (petbot.local)</em></span></label>
    <div id="creds" style="margin-top:8px">
      <input type="text" id="ssid" placeholder="Home WiFi SSID" autocomplete="off">
      <input type="password" id="pass" placeholder="WiFi password" autocomplete="off">
    </div>
  </div>
  <div class="actions">
    <button class="btn primary" onclick="applyWiFi()">Apply &amp; Reboot</button>
  </div>
  <p class="hint" style="margin-top:14px">Stuck in Home mode after switching? Power off, hold <b>BOOT</b>, power back on, keep holding <b>3 s</b>. Marvin wipes the saved WiFi and comes back in AP mode.</p>
</section>

<div class="toast" id="toast"></div>

<script>
const $=id=>document.getElementById(id);
const fetchTxt=u=>fetch(u).then(r=>r.text());
function toast(msg){const t=$('toast');t.textContent=msg;t.classList.add('show');clearTimeout(window._tt);window._tt=setTimeout(()=>t.classList.remove('show'),1400);}

// Tabs
document.querySelectorAll('nav.tabs button').forEach(b=>{
  b.onclick=()=>{
    document.querySelectorAll('nav.tabs button').forEach(x=>x.classList.remove('active'));
    document.querySelectorAll('.tab').forEach(x=>x.classList.remove('active'));
    b.classList.add('active');
    $('tab-'+b.dataset.tab).classList.add('active');
  };
});

// Radio styling
document.querySelectorAll('.radio input').forEach(r=>{
  r.onchange=()=>{document.querySelectorAll('.radio').forEach(l=>l.classList.remove('checked'));r.closest('.radio').classList.add('checked');};
});

function camOff(){$('cam').style.display='none';$('cam-off').style.display='flex';}
function c(cmd){return fetchTxt('/cmd?c='+encodeURIComponent(cmd)).then(toast);}
function f(face){return fetchTxt('/face?n='+encodeURIComponent(face)).then(toast);}
function stop(){return fetchTxt('/stop').then(toast);}
function ledToggle(which){
  fetchTxt('/led?which='+which+'&state=toggle').then(t=>{
    toast(t);
    const on=/ON$/i.test(t);
    const btn=$('btn-'+which);
    btn.textContent=(which==='flash'?'💡 Flash LED · ':'🔴 Status LED · ')+(on?'on':'off');
    btn.classList.toggle('primary',on);
  });
}

// Status pill
function refreshStatus(){
  fetchTxt('/status').then(t=>{$('status').classList.add('on');$('status').querySelector('span').textContent=t;}).catch(()=>{$('status').classList.remove('on');$('status').querySelector('span').textContent='offline';});
}
setInterval(refreshStatus,2000);refreshStatus();

// Joystick
const stick={base:$('jb'),nub:$('jn'),active:false,cx:0,cy:0,r:78,last:0,x:0,y:0};
function jStart(e){e.preventDefault();const r=stick.base.getBoundingClientRect();stick.cx=r.left+r.width/2;stick.cy=r.top+r.height/2;stick.active=true;jMove(e);}
function jMove(e){
  if(!stick.active)return;e.preventDefault();
  const t=e.touches?e.touches[0]:e;
  let dx=t.clientX-stick.cx,dy=t.clientY-stick.cy;
  const d=Math.hypot(dx,dy);
  if(d>stick.r){dx=dx*stick.r/d;dy=dy*stick.r/d;}
  stick.nub.style.transform=`translate(${dx}px,${dy}px)`;
  stick.x=dx/stick.r;stick.y=-dy/stick.r;
  jSend();
}
function jEnd(){
  if(!stick.active)return;
  stick.active=false;stick.x=stick.y=0;
  stick.nub.style.transform='translate(0,0)';
  fetchTxt('/joy?x=0&y=0');
  $('mode-label').textContent='idle';$('speed-label').textContent='0%';$('speed-bar').value=0;
}
function jSend(){
  const now=Date.now();
  if(now-stick.last<80)return;stick.last=now;
  fetchTxt(`/joy?x=${stick.x.toFixed(2)}&y=${stick.y.toFixed(2)}`).then(()=>{
    const spd=Math.round(Math.hypot(stick.x,stick.y)*100);
    $('speed-bar').value=spd;$('speed-label').textContent=spd+'%';
    $('mode-label').textContent=spd>70?'running':spd>10?'walking':'idle';
  });
}
stick.base.addEventListener('mousedown',jStart);
stick.base.addEventListener('touchstart',jStart,{passive:false});
window.addEventListener('mousemove',jMove);
window.addEventListener('touchmove',jMove,{passive:false});
window.addEventListener('mouseup',jEnd);
window.addEventListener('touchend',jEnd);

// Calibration
const LEGS=[
  {n:'Front-Left', j:[['hip',3],['thigh',1],['calf',2]]},
  {n:'Front-Right',j:[['hip',15],['thigh',14],['calf',13]]},
  {n:'Back-Left',  j:[['hip',7],['thigh',6],['calf',5]]},
  {n:'Back-Right', j:[['hip',8],['thigh',9],['calf',10]]},
];
function buildLegs(){
  const root=$('legs');root.innerHTML='';
  LEGS.forEach(L=>{
    const card=document.createElement('div');card.className='card leg-group';
    card.innerHTML=`<div class="group-title">${L.n}</div>`;
    L.j.forEach(([jn,ch])=>{
      const row=document.createElement('div');row.className='row';
      row.innerHTML=`<span class="ch">ch ${ch}</span><span class="joint">${jn}</span><input type="range" min="500" max="2500" value="1500" data-ch="${ch}"><span class="us" id="u${ch}">1500</span><button class="row-btn" onclick="r(${ch})">R</button>`;
      row.querySelector('input').oninput=e=>{const v=parseInt(e.target.value);$('u'+ch).textContent=v;fetch(`/servo?ch=${ch}&us=${v}`);};
      card.appendChild(row);
    });
    root.appendChild(card);
  });
  fetchTxt('/home').then(t=>{if(!t)return;t.split(',').forEach(p=>{const[c,u]=p.split('=').map(Number);const s=document.querySelector(`input[data-ch="${c}"]`);if(s){s.value=u;$('u'+c).textContent=u;}});}).catch(()=>{});
}
buildLegs();
function r(ch){fetch('/release?ch='+ch);}
function releaseAll(){fetch('/release_all').then(()=>toast('all released'));}
function saveHome(){fetchTxt('/save_home').then(toast);}
function goHome(){fetchTxt('/load_home').then(()=>{toast('returning home');buildLegs();});}

// WiFi settings
function applyWiFi(){
  const m=document.querySelector('input[name=wm]:checked');
  if(!m){toast('Pick a mode first');return;}
  const p=new URLSearchParams({mode:m.value,ssid:$('ssid').value,pass:$('pass').value});
  fetchTxt('/set_wifi?'+p).then(toast);
}
</script>
</body></html>)HTML";

// ─── HTTP handlers ──────────────────────────────────────────────────────
static esp_err_t handle_root(httpd_req_t* r) {
    httpd_resp_set_type(r, "text/html");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
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

static bool get_q_float(const char* q, const char* k, float* out) {
    char buf[20] = {};
    if (httpd_query_key_value(q, k, buf, sizeof(buf)) != ESP_OK) return false;
    *out = atof(buf);
    return true;
}

static bool get_q_int(const char* q, const char* k, int* out) {
    char buf[20] = {};
    if (httpd_query_key_value(q, k, buf, sizeof(buf)) != ESP_OK) return false;
    *out = atoi(buf);
    return true;
}

static esp_err_t handle_cmd(httpd_req_t* r) {
    char q[80] = {}, reply[64] = "ok";
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK) {
        char raw[48] = {}, dec[48] = {};
        if (httpd_query_key_value(q, "c", raw, sizeof(raw)) == ESP_OK) {
            url_decode(dec, raw, sizeof(dec));
            Serial.print("[cmd] "); Serial.println(dec);
            if (strcmp(dec, "HOME") == 0) {
                s_gait_cmd.active = false;
                s_gait_cmd.speed = 0;
                s_gait_request_home = true;
                snprintf(reply, sizeof(reply), "ok: returning home");
            } else {
                snprintf(reply, sizeof(reply), "ok:%s", dec);
            }
        }
    }
    httpd_resp_sendstr(r, reply);
    return ESP_OK;
}

static esp_err_t handle_face(httpd_req_t* r) {
    char q[64] = {}, nameBuf[24] = {};
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK)
        httpd_query_key_value(q, "n", nameBuf, sizeof(nameBuf));
    if (!nameBuf[0]) return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "need n=NAME");
    Serial.print("FACE:"); Serial.println(nameBuf);
    char reply[40];
    snprintf(reply, sizeof(reply), "face = %s", nameBuf);
    httpd_resp_sendstr(r, reply);
    return ESP_OK;
}

static esp_err_t handle_joy(httpd_req_t* r) {
    char q[64] = {};
    float x = 0, y = 0;
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK) {
        get_q_float(q, "x", &x);
        get_q_float(q, "y", &y);
    }
    if (x < -1) x = -1; if (x > 1) x = 1;
    if (y < -1) y = -1; if (y > 1) y = 1;
    s_gait_cmd.x = x;
    s_gait_cmd.y = y;
    s_gait_cmd.speed = sqrtf(x * x + y * y);
    s_gait_cmd.run_mode = (s_gait_cmd.speed > 0.7f);
    s_gait_cmd.active = (s_gait_cmd.speed > 0.1f);
    if (s_gait_cmd.active) s_gait_request_home = false;
    httpd_resp_sendstr(r, "ok");
    return ESP_OK;
}

static esp_err_t handle_stop(httpd_req_t* r) {
    s_gait_cmd.active = false;
    s_gait_cmd.speed = 0;
    s_gait_cmd.run_mode = false;
    s_gait_request_home = true;
    httpd_resp_sendstr(r, "stopping");
    return ESP_OK;
}

static esp_err_t handle_servo(httpd_req_t* r) {
    char q[64] = {};
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) != ESP_OK)
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "no query");
    int ch, us;
    if (!get_q_int(q, "ch", &ch) || !get_q_int(q, "us", &us))
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "need ch,us");
    if (ch < 0 || ch > 15) return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "bad ch");
    s_gait_cmd.active = false;
    s_gait_request_home = false;
    servo_set_us((uint8_t)ch, (uint16_t)us);
    s_current_us[ch] = s_servo_us[ch];
    httpd_resp_sendstr(r, "ok");
    return ESP_OK;
}

static esp_err_t handle_release(httpd_req_t* r) {
    char q[32] = {};
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK) {
        int ch;
        if (get_q_int(q, "ch", &ch) && ch >= 0 && ch <= 15) {
            s_gait_cmd.active = false;
            servo_release((uint8_t)ch);
        }
    }
    httpd_resp_sendstr(r, "ok");
    return ESP_OK;
}

static esp_err_t handle_release_all(httpd_req_t* r) {
    s_gait_cmd.active = false;
    all_release();
    httpd_resp_sendstr(r, "ok released");
    return ESP_OK;
}

static esp_err_t handle_save_home(httpd_req_t* r) {
    save_home();
    httpd_resp_sendstr(r, "home pose saved");
    return ESP_OK;
}

static esp_err_t handle_load_home(httpd_req_t* r) {
    load_home();
    s_gait_cmd.active = false;
    s_gait_request_home = true;
    httpd_resp_sendstr(r, "ok loaded");
    return ESP_OK;
}

static esp_err_t handle_home_json(httpd_req_t* r) {
    char buf[320]; int n = 0;
    for (uint8_t i = 0; i < 12; i++) {
        n += snprintf(buf + n, sizeof(buf) - n, "%s%u=%u",
                      i ? "," : "", LEG_CH[i], s_home_us[LEG_CH[i]]);
    }
    httpd_resp_sendstr(r, buf);
    return ESP_OK;
}

static esp_err_t handle_status(httpd_req_t* r) {
    char buf[200];
    const char* face = s_face_state ? s_face_state : "IDLE";
    snprintf(buf, sizeof(buf),
             "%s @ %s · %s · cam=%s · pca=%s · touch=%s · up=%lus",
             s_active_mode.c_str(), s_active_ip.c_str(), face,
             s_camera_ok ? "on" : "off",
             s_pca_ok ? "on" : "off",
             s_touch_pressed ? "yes" : "no",
             (unsigned long)(millis() / 1000));
    httpd_resp_sendstr(r, buf);
    return ESP_OK;
}

static esp_err_t handle_touch(httpd_req_t* r) {
    httpd_resp_sendstr(r, s_touch_pressed ? "1" : "0");
    return ESP_OK;
}

// /led?which=flash|status&state=on|off|toggle
static esp_err_t handle_led(httpd_req_t* r) {
    char q[64] = {}, whichBuf[16] = {}, stateBuf[16] = {};
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK) {
        httpd_query_key_value(q, "which", whichBuf, sizeof(whichBuf));
        httpd_query_key_value(q, "state", stateBuf, sizeof(stateBuf));
    }
    bool target = false;
    bool is_flash = (strcmp(whichBuf, "flash") == 0);
    bool is_status = (strcmp(whichBuf, "status") == 0);
    if (!is_flash && !is_status) {
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "which=flash|status");
    }
    if      (strcmp(stateBuf, "on")     == 0) target = true;
    else if (strcmp(stateBuf, "off")    == 0) target = false;
    else if (strcmp(stateBuf, "toggle") == 0) target = is_flash ? !s_led_flash_on : !s_led_status_on;
    else target = false;
    if (is_flash) led_flash(target);
    else          led_status(target);
    char reply[40];
    snprintf(reply, sizeof(reply), "%s LED %s", is_flash ? "flash" : "status", target ? "ON" : "OFF");
    httpd_resp_sendstr(r, reply);
    return ESP_OK;
}

static esp_err_t handle_set_wifi(httpd_req_t* r) {
    char q[256] = {};
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) != ESP_OK)
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "no query");
    char modeBuf[4] = {}, ssidBuf[64] = {}, passBuf[64] = {};
    char ssidDec[64] = {}, passDec[64] = {};
    httpd_query_key_value(q, "mode", modeBuf, sizeof(modeBuf));
    httpd_query_key_value(q, "ssid", ssidBuf, sizeof(ssidBuf));
    httpd_query_key_value(q, "pass", passBuf, sizeof(passBuf));
    url_decode(ssidDec, ssidBuf, sizeof(ssidDec));
    url_decode(passDec, passBuf, sizeof(passDec));
    s_wifi_mode = (uint8_t)atoi(modeBuf);
    s_home_ssid = String(ssidDec);
    s_home_pass = String(passDec);
    save_wifi_cfg();
    httpd_resp_sendstr(r, "Saved. Rebooting...");
    delay(2000);
    ESP.restart();
    return ESP_OK;
}

static esp_err_t handle_stream(httpd_req_t* req) {
    if (!s_camera_ok) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no cam");
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

// ─── Bring-up helpers ───────────────────────────────────────────────────
static void diag_chip_info() {
    Serial.printf("[diag] PSRAM size : %u bytes\n", (unsigned)ESP.getPsramSize());
    Serial.printf("[diag] Free heap  : %u bytes\n", (unsigned)ESP.getFreeHeap());
    Serial.printf("[diag] Free PSRAM : %u bytes\n", (unsigned)ESP.getFreePsram());
    Serial.printf("[diag] CPU MHz    : %lu\n", (unsigned long)getCpuFrequencyMhz());
    Serial.printf("[diag] Flash size : %u bytes\n", (unsigned)ESP.getFlashChipSize());
}

static bool try_camera_init(uint32_t xclk_hz, int fb_count) {
    camera_config_t cfg = {};
    cfg.ledc_channel = LEDC_CHANNEL_0; cfg.ledc_timer = LEDC_TIMER_0;
    cfg.pin_d0 = CAM_D0; cfg.pin_d1 = CAM_D1; cfg.pin_d2 = CAM_D2;
    cfg.pin_d3 = CAM_D3; cfg.pin_d4 = CAM_D4; cfg.pin_d5 = CAM_D5;
    cfg.pin_d6 = CAM_D6; cfg.pin_d7 = CAM_D7;
    cfg.pin_xclk = CAM_XCLK; cfg.pin_pclk = CAM_PCLK;
    cfg.pin_vsync = CAM_VSYNC; cfg.pin_href = CAM_HREF;
    cfg.pin_sscb_sda = CAM_SIOD; cfg.pin_sscb_scl = CAM_SIOC;
    cfg.pin_pwdn = CAM_PWDN;  cfg.pin_reset = CAM_RESET;
    cfg.xclk_freq_hz = xclk_hz;
    cfg.pixel_format = PIXFORMAT_JPEG;
    cfg.frame_size   = FRAMESIZE_QVGA;
    cfg.jpeg_quality = 12;
    cfg.fb_count     = fb_count;
    return esp_camera_init(&cfg) == ESP_OK;
}

static bool init_camera() {
    diag_chip_info();
    Serial.println("[cam] trying 20 MHz XCLK, fb=2…");
    if (try_camera_init(20000000, 2)) { Serial.println("[cam] OV camera ready @ 20 MHz"); return true; }
    Serial.println("[cam] failed — retrying 10 MHz, fb=2…");
    if (try_camera_init(10000000, 2)) { Serial.println("[cam] camera ready @ 10 MHz"); return true; }
    Serial.println("[cam] failed — retrying 20 MHz, fb=1…");
    if (try_camera_init(20000000, 1)) { Serial.println("[cam] camera ready (single fb)"); return true; }
    Serial.println("[cam] ALL attempts failed");
    Serial.println("[cam] check: (1) Arduino-ESP32 board package version 3.x for OV3660 PID");
    Serial.println("[cam]        (2) ribbon cable seated, not flipped");
    Serial.println("[cam]        (3) PSRAM = Enabled in Tools menu");
    return false;
}

static bool init_pca9685() {
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);
    if (!s_pca.begin()) {
        Serial.println("[pca] begin FAILED — check SDA=13 SCL=14 + V+ supply");
        return false;
    }
    s_pca.setOscillatorFrequency(27000000);
    s_pca.setPWMFreq(50);
    for (uint8_t i = 0; i < 12; i++) {
        servo_set_us(LEG_CH[i], s_home_us[LEG_CH[i]]);
        delay(15);
    }
    Serial.println("[pca] PCA9685 ready, servos at home");
    return true;
}

static bool init_wifi_sta() {
    if (!s_home_ssid.length()) return false;
    Serial.printf("[wifi] STA → %s\n", s_home_ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(s_home_ssid.c_str(), s_home_pass.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[wifi] STA join FAILED");
        return false;
    }
    s_active_ip   = WiFi.localIP().toString();
    s_active_mode = "STA(" + s_home_ssid + ")";
    Serial.printf("[wifi] STA IP: %s\n", s_active_ip.c_str());
    if (MDNS.begin("petbot")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("[mdns] http://petbot.local");
    }
    return true;
}

static void init_wifi_ap() {
    uint64_t mac = ESP.getEfuseMac();
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "PetBot_%04x", (unsigned)(mac & 0xFFFF));
    WiFi.mode(WIFI_AP);
    WiFi.softAP(s_ap_ssid, "petbot123");
    s_active_ip   = WiFi.softAPIP().toString();
    s_active_mode = "AP";
    Serial.printf("[wifi] AP: %s  pw petbot123  IP %s\n", s_ap_ssid, s_active_ip.c_str());
}

static void init_wifi() {
    if (s_wifi_mode == 1 && init_wifi_sta()) return;
    init_wifi_ap();
}

static void init_http() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 20;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        Serial.println("[http] httpd_start FAILED");
        return;
    }
    static const httpd_uri_t routes[] = {
        { "/",            HTTP_GET, handle_root,         nullptr },
        { "/stream",      HTTP_GET, handle_stream,       nullptr },
        { "/cmd",         HTTP_GET, handle_cmd,          nullptr },
        { "/face",        HTTP_GET, handle_face,         nullptr },
        { "/joy",         HTTP_GET, handle_joy,          nullptr },
        { "/stop",        HTTP_GET, handle_stop,         nullptr },
        { "/servo",       HTTP_GET, handle_servo,        nullptr },
        { "/release",     HTTP_GET, handle_release,      nullptr },
        { "/release_all", HTTP_GET, handle_release_all,  nullptr },
        { "/save_home",   HTTP_GET, handle_save_home,    nullptr },
        { "/load_home",   HTTP_GET, handle_load_home,    nullptr },
        { "/home",        HTTP_GET, handle_home_json,    nullptr },
        { "/status",      HTTP_GET, handle_status,       nullptr },
        { "/touch",       HTTP_GET, handle_touch,        nullptr },
        { "/led",         HTTP_GET, handle_led,          nullptr },
        { "/set_wifi",    HTTP_GET, handle_set_wifi,     nullptr },
    };
    for (auto& u : routes) httpd_register_uri_handler(s_httpd, &u);
    Serial.println("[http] web UI active");
}

// Hold GPIO 0 (BOOT) low for 3 s at boot → wipe WiFi config → AP mode.
static void maybe_reset_wifi_cfg() {
    pinMode(0, INPUT_PULLUP);
    delay(50);
    if (digitalRead(0) != LOW) return;
    Serial.println("[wifi] GPIO 0 low — hold 3 s to wipe WiFi config...");
    uint32_t start = millis();
    while (digitalRead(0) == LOW && millis() - start < 3000) delay(50);
    if (millis() - start >= 3000) {
        Serial.println("[wifi] credentials wiped → AP mode next boot");
        clear_wifi_cfg();
        delay(500);
        ESP.restart();
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=== Marvin (PetBot body) booting ===");

    maybe_reset_wifi_cfg();
    load_home();
    load_wifi_cfg();

    if (TOUCH_PIN >= 0) {
        pinMode(TOUCH_PIN, INPUT);
        Serial.printf("[touch] sensor on GPIO %d\n", TOUCH_PIN);
    }

    // Force both onboard LEDs OFF at boot — they default high otherwise.
    leds_init();
    Serial.println("[led] flash + status forced OFF");

    s_camera_ok = init_camera();
    s_pca_ok    = init_pca9685();
    init_wifi();
    init_http();

    // Gait task on core 1 (WiFi runs on core 0).
    xTaskCreatePinnedToCore(gait_task_fn, "gait", 4096, NULL, 1, NULL, 1);

    Serial.println("=== Marvin ready ===");
}

void loop() {
    poll_touch();
    delay(50);
}
