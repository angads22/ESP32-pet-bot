/*
 *  PetBot — Servo Calibration Sketch (standalone)
 *  ─────────────────────────────────────────────
 *  Board     : Freenove ESP32-WROVER CAM (classic ESP32) + PCA9685
 *  Purpose   : ONE job — let you find the per-servo home pulse-widths
 *              and print them as a C array you can paste into petbot.ino.
 *
 *  No camera, no BLE, no tabbed UI, no animations. Just:
 *    - AP "PetBot_Cal" (password "petbot123") at 192.168.4.1
 *    - 12 sliders, live µs feedback
 *    - "Release leg" buttons (limp the servos so you can move by hand)
 *    - "Show home values" → big block of pasteable C code
 *
 *  ── Required library ─────────────────────────────────────────────────
 *    - Adafruit PWM Servo Driver Library    by Adafruit
 *
 *  ── Tools menu ───────────────────────────────────────────────────────
 *    Board                  : AI Thinker ESP32-CAM
 *    Flash Mode             : QIO        Flash Size : 4MB (32Mb)
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
 *    4. Per leg: tap "Release" → move it by hand to where it should sit
 *       in the neutral stand pose → drag the slider until the servo grabs
 *       at that position. Tighten servo horn if needed.
 *    5. Repeat for all 12 servos until the dog stands cleanly.
 *    6. Tap "Show home values" — a big block of C code appears.
 *    7. Copy that block; paste it back to me or directly into petbot.ino
 *       (replace HOME_US[]).
 */

#include <Arduino.h>
#include <WiFi.h>
#include "esp_http_server.h"
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

#define I2C_SDA       13
#define I2C_SCL       14
#define PCA9685_ADDR  0x40

// Freenove default servo channel layout
const uint8_t LEG_CH[12]   = { 0, 1, 2,  5, 6, 7,  8, 9, 10, 13, 14, 15 };
const char*   LEG_NAME[12] = {
    "FL hip", "FL thigh", "FL calf",
    "FR hip", "FR thigh", "FR calf",
    "BL hip", "BL thigh", "BL calf",
    "BR hip", "BR thigh", "BR calf",
};

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
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent;font-family:system-ui,sans-serif}
body{background:#0a0a14;color:#e7e9ee;margin:0 auto;padding:14px;max-width:560px}
h1{color:#e94560;margin:6px 0 4px;font-size:18px;text-align:center}
.hint{font-size:12px;color:#9aa0b4;line-height:1.5;margin:6px 0 12px}
.leg{background:#0f1322;border:1px solid #1f2236;border-radius:8px;padding:10px;margin-bottom:10px}
.leg h3{margin:0 0 6px;font-size:12px;color:#e94560;text-transform:uppercase;letter-spacing:.06em}
.row{display:flex;align-items:center;gap:8px;margin:6px 0}
.row label{font-size:13px;flex:0 0 70px;color:#9aa0b4}
.row input[type=range]{flex:1;accent-color:#e94560}
.row .us{font:12px ui-monospace,monospace;color:#9aa0b4;min-width:42px;text-align:right}
.row button{padding:6px 10px;background:#16213e;color:#fff;border:1px solid #1f2236;border-radius:6px;font-size:12px;cursor:pointer}
.actions{display:flex;flex-wrap:wrap;gap:8px;margin:14px 0}
.actions button{flex:1;min-width:120px;padding:12px;background:#16213e;color:#fff;border:2px solid #1f2236;border-radius:8px;font-size:14px;cursor:pointer}
.actions button.primary{background:#e94560;border-color:#e94560}
pre#out{background:#0f1322;border:1px solid #1f2236;border-radius:8px;padding:10px;color:#a0e0a0;font:12px ui-monospace,monospace;overflow-x:auto;white-space:pre-wrap;display:none}
.copy{display:none;margin-top:6px}
</style></head><body>
<h1>PetBot calibration</h1>
<p class="hint">Per leg: tap <b>Release</b> → move the leg with your hand to where it should sit. Drag the slider until the servo "grabs" at that position. Repeat for all 12 servos. When the dog stands cleanly, tap <b>Show home values</b> and paste the output into petbot.ino.</p>

<div id="legs"></div>

<div class="actions">
  <button class="primary" onclick="showHome()">Show home values</button>
  <button onclick="releaseAll()">Release all</button>
  <button onclick="centerAll()">All to 1500 µs</button>
</div>

<pre id="out"></pre>
<button class="copy" id="copy" onclick="copyOut()">Copy</button>

<script>
const $=id=>document.getElementById(id);
const LEGS=[
  ['Front-Left',  [['hip',0],['thigh',1],['calf',2]]],
  ['Front-Right', [['hip',5],['thigh',6],['calf',7]]],
  ['Back-Left',   [['hip',8],['thigh',9],['calf',10]]],
  ['Back-Right',  [['hip',13],['thigh',14],['calf',15]]],
];
const NAMES={0:'FL hip',1:'FL thigh',2:'FL calf',5:'FR hip',6:'FR thigh',7:'FR calf',8:'BL hip',9:'BL thigh',10:'BL calf',13:'BR hip',14:'BR thigh',15:'BR calf'};

function build(){
  const root=$('legs'); root.innerHTML='';
  LEGS.forEach(([gname, joints])=>{
    const g=document.createElement('div'); g.className='leg';
    g.innerHTML=`<h3>${gname}</h3>`;
    joints.forEach(([jname, ch])=>{
      const row=document.createElement('div'); row.className='row';
      row.innerHTML=`
        <label>${jname}</label>
        <input type="range" min="500" max="2500" value="1500" data-ch="${ch}">
        <span class="us" id="u${ch}">1500</span>
        <button onclick="r(${ch})">Release</button>`;
      const slider=row.querySelector('input');
      slider.oninput=e=>{
        const v=parseInt(e.target.value);
        $('u'+ch).textContent=v;
        fetch(`/servo?ch=${ch}&us=${v}`);
      };
      g.appendChild(row);
    });
    root.appendChild(g);
  });
}
build();

function r(ch){fetch('/release?ch='+ch)}
function releaseAll(){fetch('/release_all')}
function centerAll(){
  document.querySelectorAll('input[type=range]').forEach(s=>{
    s.value=1500; $('u'+s.dataset.ch).textContent=1500;
    fetch(`/servo?ch=${s.dataset.ch}&us=1500`);
  });
}

function showHome(){
  fetch('/home').then(r=>r.json()).then(j=>{
    let s = '// Paste into petbot.ino, replacing the default home values:\n';
    s += 'static const uint16_t HOME_US[12] = {\n';
    [0,1,2,5,6,7,8,9,10,13,14,15].forEach(ch=>{
      const us = j[ch] || 1500;
      const name = NAMES[ch].padEnd(9);
      s += `    /* ${name} ch ${String(ch).padStart(2)} */ ${us},\n`;
    });
    s += '};\n';
    $('out').textContent = s;
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
    Serial.println("\n=== PetBot Calibration ===");

    // PCA9685
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

    // AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP("PetBot_Cal", "petbot123");
    Serial.printf("[wifi] AP: PetBot_Cal  pw: petbot123  IP: %s\n",
                  WiFi.softAPIP().toString().c_str());

    // HTTP
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 8;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        Serial.println("[http] start FAILED");
        return;
    }
    static const httpd_uri_t routes[] = {
        { "/",            HTTP_GET, h_root,        nullptr },
        { "/servo",       HTTP_GET, h_servo,       nullptr },
        { "/release",     HTTP_GET, h_release,     nullptr },
        { "/release_all", HTTP_GET, h_release_all, nullptr },
        { "/home",        HTTP_GET, h_home,        nullptr },
    };
    for (auto& u : routes) httpd_register_uri_handler(s_httpd, &u);
    Serial.println("[http] up — open http://192.168.4.1");
}

void loop() {
    delay(100);
}
