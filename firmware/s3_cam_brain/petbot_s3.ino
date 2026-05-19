/*
 *  PetBot / Marvin — Servo Calibration Sketch (standalone)
 *  ────────────────────────────────────────────────────────
 *  Board     : Freenove ESP32-WROVER CAM (classic ESP32) + PCA9685
 *  Purpose   : Find the home pulse-widths for the 12 leg servos and
 *              produce a paste-able list to bake into petbot.ino.
 *
 *  Layout (this robot's confirmed mapping):
 *
 *      Front-Left          Front-Right
 *        ch 3  hip           ch 15 hip
 *        ch 1  thigh         ch 14 thigh
 *        ch 2  calf          ch 13 calf
 *
 *      Back-Left           Back-Right
 *        ch 7  hip           ch 8  hip
 *        ch 6  thigh         ch 9  thigh
 *        ch 5  calf          ch 10 calf
 *
 *  ── Required library ─────────────────────────────────────────────────
 *    - Adafruit PWM Servo Driver Library    by Adafruit
 *
 *  ── Tools menu ───────────────────────────────────────────────────────
 *    Board                  : AI Thinker ESP32-CAM
 *    Flash Mode             : QIO
 *    Partition Scheme       : Default 4MB with spiffs
 *    PSRAM                  : Enabled
 *    Upload Speed           : 921600
 *
 *  ── Wiring (same as main sketch) ─────────────────────────────────────
 *    PCA9685 SDA    →  ESP32-CAM GPIO 13
 *    PCA9685 SCL    →  ESP32-CAM GPIO 14
 *    PCA9685 VCC    →  ESP32-CAM 3.3V
 *    PCA9685 GND    →  ESP32-CAM GND   (and to servo battery GND)
 *    PCA9685 V+     →  5–6 V regulated supply  (NOT raw battery!)
 *
 *  ── How to use ───────────────────────────────────────────────────────
 *    1. Flash this sketch.
 *    2. Phone → join WiFi "PetBot_Cal" (password "petbot123").
 *    3. Open http://192.168.4.1
 *    4. Per leg: tap "Release" on a joint → physically move it where it
 *       should sit in a clean standing pose → drag the slider until the
 *       servo grabs at that position. Repeat for all 12.
 *    5. (Optional) Tap "Wave demo" — each leg's calf will briefly lift
 *       its foot in sequence to verify all servos articulate cleanly.
 *    6. Tap "Show home values" → screenshot or copy the output and
 *       send it to me to bake into petbot.ino.
 */

#include <Arduino.h>
#include <WiFi.h>
#include "esp_http_server.h"
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

#define I2C_SDA       13
#define I2C_SCL       14
#define PCA9685_ADDR  0x40

// Active channels (12 of the 16 PCA9685 outputs are wired to servos).
const uint8_t LEG_CH[12] = { 1, 2, 3,  5, 6, 7,  8, 9, 10, 13, 14, 15 };

// The 4 "calf" channels (each leg's bottom joint) — used by Wave demo.
const uint8_t CALF_CH[4] = { 2, 13, 5, 10 };   // FL, FR, BL, BR
const uint8_t THIGH_CH[4] = { 1, 14, 6, 9 };   // FL, FR, BL, BR
const uint8_t HIP_CH[4] = { 3, 15, 7, 8 };     // FL, FR, BL, BR

static Adafruit_PWMServoDriver pca(PCA9685_ADDR);
static uint16_t s_us[16] = {0};
static bool     s_pca_ok = false;
static httpd_handle_t s_httpd = nullptr;

static inline uint16_t us_to_ticks(uint16_t us) {
    return (uint32_t)us * 4096UL / 20000UL;
}

static void servo_set_us(uint8_t ch, uint16_t us) {
    if (us < 400)  us = 400;
    if (us > 2600) us = 2600;
    s_us[ch] = us;
    if (s_pca_ok) pca.setPWM(ch, 0, us_to_ticks(us));
}

static void servo_release(uint8_t ch) {
    if (s_pca_ok) pca.setPWM(ch, 0, 4096);
}

static const char PAGE_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PetBot calibration</title>
<style>
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent;font-family:-apple-system,BlinkMacSystemFont,"SF Pro Display","Segoe UI",system-ui,sans-serif}
body{background:radial-gradient(110% 110% at 10% -10%,#2a3559 0%,#101426 40%,#070910 100%);color:#f2f4f8;margin:0 auto;padding:14px;max-width:560px}
h1{margin:8px 0 6px;font-size:20px;letter-spacing:.01em;text-align:center;color:#fff}
.hint{font-size:12px;color:#d2d7e4;line-height:1.55;margin:8px 0 14px;padding:12px;border:1px solid rgba(255,255,255,.18);border-radius:16px;background:rgba(255,255,255,.09);backdrop-filter:blur(12px)}
.leg-group{background:rgba(255,255,255,.08);border:1px solid rgba(255,255,255,.22);border-radius:16px;padding:12px 12px 10px;margin-bottom:12px;backdrop-filter:blur(14px)}
.leg-group h3{margin:0 0 8px;color:#1ed760;font-size:13px;text-transform:uppercase;letter-spacing:.09em}
.row{display:flex;align-items:center;gap:6px;margin:7px 0}
.row .ch{font:600 12px ui-monospace,monospace;color:#f7fbff;flex:0 0 44px}
.row .joint{font-size:12px;color:#d2d7e4;flex:0 0 44px}
.row input[type=range]{flex:1;accent-color:#1ed760;min-width:0}
.row .us{font:12px ui-monospace,monospace;color:#d2d7e4;min-width:48px;text-align:right}
.row button{padding:5px 8px;background:rgba(255,255,255,.12);color:#fff;border:1px solid rgba(255,255,255,.25);border-radius:999px;font-size:11px;cursor:pointer}
.row button.w{background:#1db954;border-color:#1db954}
.actions{display:flex;flex-wrap:wrap;gap:8px;margin:14px 0}
.actions button{flex:1;min-width:120px;padding:12px;background:rgba(255,255,255,.12);color:#fff;border:1px solid rgba(255,255,255,.26);border-radius:14px;font-size:14px;font-weight:600;cursor:pointer}
.actions button.primary{background:#1db954;border-color:#1db954;color:#08140a}
.actions button.demo{background:linear-gradient(135deg,#1ed760,#1aa34a);border-color:#1ed760;color:#08140a}
pre#out{background:rgba(2,6,18,.72);border:1px solid rgba(255,255,255,.2);border-radius:14px;padding:10px;color:#b6f9c8;font:12px ui-monospace,monospace;overflow-x:auto;white-space:pre-wrap;display:none;margin-top:8px}
.copy{display:none;margin-top:8px;padding:8px 12px;border-radius:999px;border:1px solid rgba(255,255,255,.3);background:rgba(255,255,255,.12);color:#fff}
</style></head><body>
<h1>PetBot calibration</h1>
<p class="hint">
  Per joint: tap <b>Release</b> → physically move that joint where it should sit in a clean standing pose → drag the slider until the servo grabs at that position. Use the green <b>Wiggle</b> button if you forget which servo is which. When the whole dog stands cleanly, tap <b>Show home values</b> below, screenshot or copy the output, and send it back.
</p>

<div id="legs"></div>

<div class="actions">
  <button class="primary" onclick="showHome()">Show home values</button>
  <button class="demo" onclick="walk()">Walk cycle</button>
  <button class="demo" onclick="demo()">Wave demo</button>
  <button onclick="releaseAll()">Release all</button>
  <button onclick="centerAll()">All to 1500 µs</button>
</div>

<pre id="out"></pre>
<button class="copy" id="copy" onclick="copyOut()">Copy</button>

<script>
const $=id=>document.getElementById(id);

// User's confirmed wiring. hip / thigh / calf within each leg.
const LEGS = [
  { name: 'Front-Left',  joints: [{j:'hip', ch:3},  {j:'thigh', ch:1},  {j:'calf', ch:2}]  },
  { name: 'Front-Right', joints: [{j:'hip', ch:15}, {j:'thigh', ch:14}, {j:'calf', ch:13}] },
  { name: 'Back-Left',   joints: [{j:'hip', ch:7},  {j:'thigh', ch:6},  {j:'calf', ch:5}]  },
  { name: 'Back-Right',  joints: [{j:'hip', ch:8},  {j:'thigh', ch:9},  {j:'calf', ch:10}] },
];

function build(){
  const root=$('legs'); root.innerHTML='';
  LEGS.forEach(L=>{
    const g=document.createElement('div'); g.className='leg-group';
    let html = `<h3>${L.name}</h3>`;
    L.joints.forEach(({j, ch})=>{
      html += `
        <div class="row">
          <span class="ch">ch ${ch}</span>
          <span class="joint">${j}</span>
          <input type="range" min="500" max="2500" value="1500" data-ch="${ch}">
          <span class="us" id="u${ch}">1500</span>
          <button class="w" onclick="wig(${ch})">W</button>
          <button onclick="r(${ch})">Rls</button>
        </div>`;
    });
    g.innerHTML = html;
    g.querySelectorAll('input').forEach(slider=>{
      const ch = parseInt(slider.dataset.ch);
      slider.oninput = e => {
        const v = parseInt(e.target.value);
        $('u' + ch).textContent = v;
        fetch(`/servo?ch=${ch}&us=${v}`);
      };
    });
    root.appendChild(g);
  });
}
build();

function r(ch){fetch('/release?ch='+ch)}
function wig(ch){fetch('/wiggle?ch='+ch)}
function demo(){fetch('/demo_wave')}
function walk(){fetch('/walk')}
function releaseAll(){fetch('/release_all')}
function centerAll(){
  document.querySelectorAll('input[type=range]').forEach(s=>{
    s.value=1500; $('u'+s.dataset.ch).textContent=1500;
    fetch(`/servo?ch=${s.dataset.ch}&us=1500`);
  });
}

function showHome(){
  fetch('/home').then(r=>r.json()).then(home=>{
    const lines = [];
    LEGS.forEach(L=>{
      const lbl = L.name.replace('Front-','F').replace('Back-','B').replace('Left','L').replace('Right','R');
      L.joints.forEach(({j: jname, ch})=>{
        const us = home[ch] !== undefined ? home[ch] : 1500;
        lines.push(`    /* ${lbl} ${jname.padEnd(5)} ch ${String(ch).padStart(2)} */ ${us},`);
      });
    });
    const out = '// Marvin home pulse-widths (paste into petbot.ino HOME_US[])\n' +
                'static const uint16_t HOME_US[12] = {\n' +
                lines.join('\n') + '\n' + '};\n';
    $('out').textContent = out;
    $('out').style.display = 'block';
    $('copy').style.display = 'inline-block';
  });
}

function copyOut(){
  navigator.clipboard.writeText($('out').textContent).then(
    ()=>{ $('copy').textContent='Copied!'; setTimeout(()=>$('copy').textContent='Copy',1500); },
    ()=>{ alert('Select the text above and copy manually.'); }
  );
}
</script>
</body></html>)HTML";

// ─── HTTP handlers ─────────────────────────────────────────────────────
static bool get_q_int(const char* q, const char* k, int* out) {
    char buf[20] = {};
    if (httpd_query_key_value(q, k, buf, sizeof(buf)) != ESP_OK) return false;
    *out = atoi(buf);
    return true;
}

static esp_err_t h_root(httpd_req_t* r) {
    httpd_resp_set_type(r, "text/html");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_sendstr(r, PAGE_HTML);
    return ESP_OK;
}

static esp_err_t h_servo(httpd_req_t* r) {
    char q[64] = {};
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) != ESP_OK)
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "no q");
    int ch, us;
    if (!get_q_int(q, "ch", &ch) || !get_q_int(q, "us", &us))
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "ch,us");
    if (ch < 0 || ch > 15) return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "bad ch");
    servo_set_us((uint8_t)ch, (uint16_t)us);
    httpd_resp_sendstr(r, "ok");
    return ESP_OK;
}

static esp_err_t h_release(httpd_req_t* r) {
    char q[32] = {};
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK) {
        int ch;
        if (get_q_int(q, "ch", &ch) && ch >= 0 && ch <= 15) servo_release((uint8_t)ch);
    }
    httpd_resp_sendstr(r, "ok");
    return ESP_OK;
}

static esp_err_t h_release_all(httpd_req_t* r) {
    for (uint8_t i = 0; i < 12; i++) servo_release(LEG_CH[i]);
    httpd_resp_sendstr(r, "ok");
    return ESP_OK;
}

// Twitch the servo so the user can identify which physical joint it is.
static esp_err_t h_wiggle(httpd_req_t* r) {
    char q[32] = {};
    int ch = -1;
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK) get_q_int(q, "ch", &ch);
    if (ch < 0 || ch > 15) return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "ch");
    uint16_t saved = s_us[ch];
    servo_set_us((uint8_t)ch, 1500); delay(80);
    servo_set_us((uint8_t)ch, 1700); delay(160);
    servo_set_us((uint8_t)ch, 1300); delay(160);
    servo_set_us((uint8_t)ch, saved);
    httpd_resp_sendstr(r, "ok");
    return ESP_OK;
}

// Lift each leg's foot briefly in sequence. Lets you verify all four
// calves articulate without binding before committing to a gait.
static esp_err_t h_demo_wave(httpd_req_t* r) {
    const char* names[4] = { "FL", "FR", "BL", "BR" };
    for (int i = 0; i < 4; i++) {
        uint8_t ch = CALF_CH[i];
        uint16_t saved = s_us[ch];
        Serial.printf("[demo] lift %s calf (ch %u)\n", names[i], ch);
        servo_set_us(ch, saved + 250); delay(450);
        servo_set_us(ch, saved);       delay(250);
    }
    httpd_resp_sendstr(r, "ok demo");
    return ESP_OK;
}

static inline uint16_t clamp_servo_pulse(int32_t us) {
    if (us < 400) return 400;
    if (us > 2600) return 2600;
    return (uint16_t)us;
}

static inline bool leg_in_tripod(uint8_t leg, const uint8_t tripod[2]) {
    return leg == tripod[0] || leg == tripod[1];
}

// Simple alternating-tripod walk demo:
// step A lifts FL+BR, then step B lifts FR+BL.
static esp_err_t h_walk(httpd_req_t* r) {
    const uint8_t tripodA[2] = { 0, 3 }; // FL + BR
    const uint8_t tripodB[2] = { 1, 2 }; // FR + BL
    uint16_t hipHome[4], thighHome[4], calfHome[4];
    for (uint8_t leg = 0; leg < 4; leg++) {
        hipHome[leg] = s_us[HIP_CH[leg]];
        thighHome[leg] = s_us[THIGH_CH[leg]];
        calfHome[leg] = s_us[CALF_CH[leg]];
    }

    auto step = [&hipHome, &thighHome, &calfHome](const uint8_t active[2], int16_t activeHip, int16_t supportHip) {
        for (uint8_t leg = 0; leg < 4; leg++) {
            const bool lift = leg_in_tripod(leg, active);
            const uint16_t hipBase = hipHome[leg];
            const uint16_t thighBase = thighHome[leg];
            const uint16_t calfBase = calfHome[leg];

            const int16_t hipDelta = lift ? activeHip : supportHip;
            const int16_t thighDelta = lift ? -90 : 45;
            const int16_t calfDelta = lift ? 140 : -50;

            servo_set_us(HIP_CH[leg], clamp_servo_pulse((int32_t)hipBase + hipDelta));
            servo_set_us(THIGH_CH[leg], clamp_servo_pulse((int32_t)thighBase + thighDelta));
            servo_set_us(CALF_CH[leg], clamp_servo_pulse((int32_t)calfBase + calfDelta));
        }
        // 260 ms keeps a visible but stable bench-test cadence for this gait.
        delay(260);
    };

    Serial.println("[demo] walk cycle start");
    for (uint8_t i = 0; i < 3; i++) {
        step(tripodA, +130, -90);
        step(tripodB, -130, +90);
    }

    for (uint8_t leg = 0; leg < 4; leg++) {
        servo_set_us(HIP_CH[leg], hipHome[leg]);
        servo_set_us(THIGH_CH[leg], thighHome[leg]);
        servo_set_us(CALF_CH[leg], calfHome[leg]);
    }
    Serial.println("[demo] walk cycle end");
    httpd_resp_sendstr(r, "ok walk");
    return ESP_OK;
}

static esp_err_t h_home(httpd_req_t* r) {
    char buf[320]; int n = 0;
    n += snprintf(buf + n, sizeof(buf) - n, "{");
    for (uint8_t i = 0; i < 12; i++) {
        n += snprintf(buf + n, sizeof(buf) - n, "%s\"%u\":%u",
                      i ? "," : "", LEG_CH[i], s_us[LEG_CH[i]]);
    }
    n += snprintf(buf + n, sizeof(buf) - n, "}");
    httpd_resp_set_type(r, "application/json");
    httpd_resp_sendstr(r, buf);
    return ESP_OK;
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=== Marvin servo calibration ===");

    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);
    s_pca_ok = pca.begin();
    if (!s_pca_ok) {
        Serial.println("[pca] begin FAILED — check wiring SDA=13 SCL=14");
    } else {
        pca.setOscillatorFrequency(27000000);
        pca.setPWMFreq(50);
        for (uint8_t i = 0; i < 12; i++) {
            servo_set_us(LEG_CH[i], 1500);
            delay(20);
        }
        Serial.println("[pca] ok — all servos to 1500 µs");
    }

    WiFi.mode(WIFI_AP);
    WiFi.softAP("PetBot_Cal", "petbot123");
    Serial.printf("[wifi] AP: PetBot_Cal  pw: petbot123  IP: %s\n",
                  WiFi.softAPIP().toString().c_str());

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 12;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        Serial.println("[http] start FAILED");
        return;
    }
    static const httpd_uri_t routes[] = {
        { "/",            HTTP_GET, h_root,        nullptr },
        { "/servo",       HTTP_GET, h_servo,       nullptr },
        { "/release",     HTTP_GET, h_release,     nullptr },
        { "/release_all", HTTP_GET, h_release_all, nullptr },
        { "/wiggle",      HTTP_GET, h_wiggle,      nullptr },
        { "/walk",        HTTP_GET, h_walk,        nullptr },
        { "/demo_wave",   HTTP_GET, h_demo_wave,   nullptr },
        { "/home",        HTTP_GET, h_home,        nullptr },
    };
    for (auto& u : routes) httpd_register_uri_handler(s_httpd, &u);
    Serial.println("[http] up — open http://192.168.4.1");
}

void loop() {
    delay(100);
}
