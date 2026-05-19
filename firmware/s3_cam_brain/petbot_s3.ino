/*
 *  PetBot / Marvin — Servo Calibration Sketch (standalone)
 *  ────────────────────────────────────────────────────────
 *  Board     : AI Thinker ESP32-CAM  (Freenove ESP32-WROVER CAM variant)
 *              + PCA9685 16-ch PWM servo driver ("the rug")
 *  Purpose   : Find the home pulse-widths for the 12 leg servos and
 *              produce a paste-able HOME_US[] list for petbot.ino.
 *
 *  ── PCA9685 ("the rug") wiring ───────────────────────────────────────
 *
 *    The I²C lines from the ESP32 lead to the PCA9685 servo driver board.
 *    Those two GPIOs are "the path to the rug":
 *
 *      ESP32-CAM GPIO 13  →  PCA9685 SDA
 *      ESP32-CAM GPIO 14  →  PCA9685 SCL
 *      ESP32-CAM 3.3 V    →  PCA9685 VCC  (logic)
 *      ESP32-CAM GND      →  PCA9685 GND  (and servo GND)
 *      External 5-6 V     →  PCA9685 V+   (servo power — NOT raw battery!)
 *
 *  ── Free GPIOs on the Freenove ESP32 dog board ───────────────────────
 *
 *    Used:  GPIO 13 (SDA → PCA9685)  GPIO 14 (SCL → PCA9685)
 *    Free:  GPIO 4 · 15 · 21 · 22 · 23 · 32 · 33
 *    Input-only (no output):  GPIO 34 · 35 · 36 (VP) · 39 (VN)
 *    Boot-sensitive (use with care):  GPIO 0 · 2
 *
 *  ── C6 display wiring (connect dog board → C6-LCD-1.47) ─────────────
 *
 *    Dog board (Freenove ESP32-CAM)     C6-LCD-1.47
 *    ───────────────────────────────    ──────────────────────────────
 *    GPIO 4   (free TX → C6 screen)  → GPIO 16  (Serial1 RX)
 *    GPIO 15  (free RX ← C6 reports) ← GPIO 17  (Serial1 TX)   [optional]
 *    GND                             →  GND
 *
 *    UART settings: 921 600 baud, 8N1.
 *    C6 reserved display pins (do NOT connect to these):
 *      MOSI=6  SCLK=7  CS=14  DC=15  RST=21  BL=22
 *
 *  ── Leg layout & PCA9685 channel map ─────────────────────────────────
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
 *  ── Required libraries ───────────────────────────────────────────────
 *    - Adafruit PWM Servo Driver Library    by Adafruit
 *
 *  ── PlatformIO / Arduino IDE settings ────────────────────────────────
 *    Board                  : AI Thinker ESP32-CAM  (or "esp32cam" in PIO)
 *    Flash Mode             : QIO
 *    Partition Scheme       : Default 4MB with spiffs
 *    PSRAM                  : Enabled
 *    Upload Speed           : 921600
 *
 *  ── Camera streaming (optional) ──────────────────────────────────────
 *    Compile with -DPETBOT_ENABLE_STREAM to enable the OV2640 MJPEG
 *    stream at http://192.168.4.1/stream alongside the calibration UI.
 *
 *  ── How to calibrate ─────────────────────────────────────────────────
 *    1. Flash this sketch.
 *    2. Phone → join WiFi "PetBot_Cal" (password "petbot123").
 *    3. Open http://192.168.4.1
 *    4. Per leg: tap "Rls" → physically position that joint → drag the
 *       slider until the servo grabs at the target angle. Use "W" to
 *       wiggle and identify a joint if unsure. Repeat all 12.
 *    5. (Optional) Tap "Wave" to verify all calves articulate.
 *    6. Tap "Show Home" → copy the output → paste into petbot.ino.
 */

#include <Arduino.h>
#include <WiFi.h>
#include "esp_http_server.h"
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

#ifdef PETBOT_ENABLE_STREAM
#include "esp_camera.h"
#endif

#define I2C_SDA       13
#define I2C_SCL       14
#define PCA9685_ADDR  0x40

// ─── AI-Thinker ESP32-CAM — OV2640 pin map ──────────────────────────────────
// Only compiled when -DPETBOT_ENABLE_STREAM is set.
#ifdef PETBOT_ENABLE_STREAM
#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK     0
#define CAM_PIN_SIOD    26   // camera I²C SDA  (separate bus from PCA9685)
#define CAM_PIN_SIOC    27   // camera I²C SCL
#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      21
#define CAM_PIN_D2      19
#define CAM_PIN_D1      18
#define CAM_PIN_D0       5
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22
static bool s_cam_ok = false;
#endif

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
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>PetBot</title>
<style>
:root{
  --bg:#0d0d0d;
  --s1:rgba(255,255,255,.055);
  --s2:rgba(255,255,255,.09);
  --bd:rgba(255,255,255,.10);
  --bd2:rgba(255,255,255,.18);
  --t1:#ffffff;
  --t2:rgba(255,255,255,.60);
  --t3:rgba(255,255,255,.35);
  --grn:#1DB954;
  --grn2:#1ED760;
  --glow:rgba(29,185,84,.35);
  --r:18px;
  --rs:12px;
}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent;margin:0;padding:0}
html,body{height:100%}
body{
  font-family:-apple-system,BlinkMacSystemFont,"SF Pro Display","Segoe UI",system-ui,sans-serif;
  background:var(--bg);color:var(--t1);
  min-height:100vh;padding-bottom:130px;
  overscroll-behavior:none;
}
body::before{
  content:'';position:fixed;inset:0;pointer-events:none;z-index:0;
  background:
    radial-gradient(ellipse 90% 50% at 15% -5%,rgba(29,185,84,.11) 0%,transparent 55%),
    radial-gradient(ellipse 60% 40% at 85% 105%,rgba(0,80,200,.09) 0%,transparent 55%);
}

/* ── Header ────────────────────────────────────────────── */
.hdr{
  position:relative;z-index:1;
  padding:48px 20px 18px;
  display:flex;align-items:center;gap:16px;
  max-width:580px;margin:0 auto;
}
.hdr-art{
  width:60px;height:60px;border-radius:14px;flex-shrink:0;
  background:linear-gradient(145deg,#1DB954 0%,#155e2d 55%,#091a0e 100%);
  display:flex;align-items:center;justify-content:center;
  font-size:26px;
  box-shadow:0 6px 24px var(--glow),0 0 0 1px rgba(29,185,84,.25);
}
.hdr-title{font-size:22px;font-weight:700;letter-spacing:-.02em}
.hdr-sub{font-size:13px;color:var(--t2);margin-top:2px}
.badge{
  margin-left:auto;
  display:inline-flex;align-items:center;gap:5px;
  padding:4px 11px;border-radius:999px;
  background:rgba(29,185,84,.13);color:var(--grn2);
  font-size:11px;font-weight:700;letter-spacing:.04em;
  border:1px solid rgba(29,185,84,.28);
}
.badge::before{
  content:'';width:6px;height:6px;border-radius:50%;background:var(--grn2);
  animation:dot 2s ease infinite;
}
@keyframes dot{0%,100%{opacity:1;transform:scale(1)}50%{opacity:.4;transform:scale(.65)}}

/* ── Hint ──────────────────────────────────────────────── */
.hint{
  position:relative;z-index:1;
  margin:0 16px 14px;padding:14px 16px;
  background:var(--s1);
  backdrop-filter:blur(22px) saturate(180%);
  -webkit-backdrop-filter:blur(22px) saturate(180%);
  border:1px solid var(--bd);border-radius:var(--rs);
  font-size:13px;color:var(--t2);line-height:1.65;
  max-width:548px;margin-left:auto;margin-right:auto;
}
.hint b{color:var(--t1)}

/* ── Stream ────────────────────────────────────────────── */
.stream-wrap{
  position:relative;z-index:1;
  margin:0 16px 14px;
  background:var(--s1);
  backdrop-filter:blur(20px);-webkit-backdrop-filter:blur(20px);
  border:1px solid var(--bd);border-radius:var(--r);
  overflow:hidden;
  max-width:548px;margin-left:auto;margin-right:auto;
}
.stream-label{
  padding:10px 14px 6px;
  font-size:10px;font-weight:700;text-transform:uppercase;letter-spacing:.10em;color:var(--t3);
}
.stream-img{width:100%;aspect-ratio:4/3;object-fit:contain;background:#000;display:block}

/* ── Leg cards grid ────────────────────────────────────── */
.legs{
  position:relative;z-index:1;
  padding:0 16px;
  display:grid;gap:12px;
  max-width:580px;margin:0 auto;
}
@media(min-width:500px){.legs{grid-template-columns:1fr 1fr}}

.card{
  background:var(--s1);
  backdrop-filter:blur(26px) saturate(160%);
  -webkit-backdrop-filter:blur(26px) saturate(160%);
  border:1px solid var(--bd);border-radius:var(--r);
  padding:16px;
  transition:border-color .2s,background .2s;
}
.card:hover{background:var(--s2);border-color:var(--bd2)}
.card-hdr{
  display:flex;align-items:center;justify-content:space-between;
  margin-bottom:14px;
}
.card-name{font-size:11px;font-weight:800;text-transform:uppercase;letter-spacing:.11em;color:var(--grn2)}
.card-icon{font-size:17px;opacity:.45}

/* ── Joint rows ────────────────────────────────────────── */
.jrow{margin-bottom:11px}
.jrow:last-child{margin-bottom:0}
.jmeta{
  display:flex;align-items:center;justify-content:space-between;
  margin-bottom:5px;
}
.jlabel{font-size:11px;color:var(--t2)}
.jright{display:flex;gap:7px;align-items:center}
.jch{font:600 10px ui-monospace,monospace;color:var(--t3)}
.jus{
  font:700 11px ui-monospace,monospace;color:var(--grn2);
  background:rgba(29,185,84,.13);padding:1px 7px;border-radius:5px;
  min-width:62px;text-align:right;
}
.jctrl{display:flex;align-items:center;gap:6px}
input[type=range]{
  flex:1;height:3px;border-radius:2px;cursor:pointer;accent-color:var(--grn);
  background:linear-gradient(to right,var(--grn) var(--pct,50%),rgba(255,255,255,.14) var(--pct,50%));
}
.bs{
  flex-shrink:0;padding:0;border-radius:8px;
  font-size:11px;font-weight:600;cursor:pointer;
  display:flex;align-items:center;justify-content:center;
  transition:all .15s;border:1px solid var(--bd);
}
.bs.wig{
  width:28px;height:28px;
  background:rgba(29,185,84,.14);color:var(--grn2);border-color:rgba(29,185,84,.30);
}
.bs.wig:hover{background:rgba(29,185,84,.26);box-shadow:0 0 0 2px var(--glow)}
.bs.rls{
  width:36px;height:28px;
  background:var(--s1);color:var(--t2);
}
.bs.rls:hover{background:var(--s2);color:var(--t1);border-color:var(--bd2)}

/* ── Output block ──────────────────────────────────────── */
.out-wrap{
  position:relative;z-index:1;
  margin:14px 16px 0;
  max-width:548px;margin-left:auto;margin-right:auto;
}
#out{
  display:none;
  background:rgba(0,0,0,.65);
  backdrop-filter:blur(14px);-webkit-backdrop-filter:blur(14px);
  border:1px solid rgba(29,185,84,.22);border-radius:var(--rs);
  padding:14px;color:#4ade80;
  font:12px ui-monospace,monospace;
  overflow-x:auto;white-space:pre-wrap;line-height:1.65;
}
#cpybtn{
  display:none;margin-top:10px;width:100%;padding:13px;
  background:var(--s1);border:1px solid var(--bd2);border-radius:var(--rs);
  color:var(--t1);font-size:14px;font-weight:600;cursor:pointer;
  transition:background .15s;
}
#cpybtn:hover{background:var(--s2)}

/* ── Bottom action bar ─────────────────────────────────── */
.bar{
  position:fixed;bottom:0;left:0;right:0;z-index:100;
  padding:12px 16px max(18px,env(safe-area-inset-bottom));
  background:rgba(10,10,10,.88);
  backdrop-filter:blur(36px) saturate(200%);
  -webkit-backdrop-filter:blur(36px) saturate(200%);
  border-top:1px solid var(--bd);
}
.bar-row{
  display:flex;gap:8px;max-width:548px;margin:0 auto;
}
.ab{
  flex:1;padding:13px 6px;
  background:var(--s1);border:1px solid var(--bd);border-radius:var(--rs);
  color:var(--t2);font-size:11px;font-weight:600;cursor:pointer;
  display:flex;flex-direction:column;align-items:center;gap:3px;
  transition:all .15s;
}
.ab .ico{font-size:19px}
.ab:hover{background:var(--s2);color:var(--t1);border-color:var(--bd2)}
.ab.primary{
  background:var(--grn);border-color:var(--grn);color:#04180a;font-weight:800;
  box-shadow:0 0 22px var(--glow);
}
.ab.primary:hover{background:var(--grn2);border-color:var(--grn2);box-shadow:0 0 32px var(--glow)}
.ab.danger{background:rgba(255,59,48,.11);border-color:rgba(255,59,48,.24);color:#ff6961}
.ab.danger:hover{background:rgba(255,59,48,.20)}
</style></head>
<body>

<div class="hdr">
  <div class="hdr-art">🐕</div>
  <div>
    <div class="hdr-title">PetBot</div>
    <div class="hdr-sub">Servo Calibration</div>
  </div>
  <div class="badge">LIVE</div>
</div>

<p class="hint">
  <b>Per joint:</b> tap <b>Rls</b> → physically position that joint →
  drag the slider until the servo grabs. Use <b>W</b> to wiggle &amp; identify a joint.
  When the dog stands cleanly, tap <b>Show Home</b> and copy the output.
</p>

<div id="stream-section" class="stream-wrap" style="display:none">
  <div class="stream-label">📷 Live Camera</div>
  <img class="stream-img" id="stream-img" src="/stream" alt="camera stream"
       onerror="document.getElementById('stream-section').style.display='none'">
</div>

<div class="legs" id="legs"></div>

<div class="out-wrap">
  <pre id="out"></pre>
  <button id="cpybtn" onclick="copyOut()">Copy to clipboard</button>
</div>

<div class="bar">
  <div class="bar-row">
    <button class="ab primary" onclick="showHome()">
      <span class="ico">📋</span>Show Home
    </button>
    <button class="ab" onclick="walk()">
      <span class="ico">🚶</span>Walk
    </button>
    <button class="ab" onclick="demo()">
      <span class="ico">👋</span>Wave
    </button>
    <button class="ab" onclick="centerAll()">
      <span class="ico">⊙</span>Center
    </button>
    <button class="ab danger" onclick="releaseAll()">
      <span class="ico">✕</span>Release
    </button>
  </div>
</div>

<script>
const $=id=>document.getElementById(id);

// Confirmed PCA9685 channel map: hip / thigh / calf per leg.
const LEGS=[
  {name:'Front-Left', icon:'↖',joints:[{j:'hip',ch:3},{j:'thigh',ch:1},{j:'calf',ch:2}]},
  {name:'Front-Right',icon:'↗',joints:[{j:'hip',ch:15},{j:'thigh',ch:14},{j:'calf',ch:13}]},
  {name:'Back-Left',  icon:'↙',joints:[{j:'hip',ch:7},{j:'thigh',ch:6},{j:'calf',ch:5}]},
  {name:'Back-Right', icon:'↘',joints:[{j:'hip',ch:8},{j:'thigh',ch:9},{j:'calf',ch:10}]},
];

function build(){
  const root=$('legs');
  LEGS.forEach(L=>{
    const card=document.createElement('div'); card.className='card';
    let h=`<div class="card-hdr"><span class="card-name">${L.name}</span><span class="card-icon">${L.icon}</span></div>`;
    L.joints.forEach(({j,ch})=>{
      h+=`<div class="jrow">
        <div class="jmeta">
          <span class="jlabel">${j}</span>
          <div class="jright"><span class="jch">ch ${ch}</span><span class="jus" id="u${ch}">1500 µs</span></div>
        </div>
        <div class="jctrl">
          <input type="range" min="500" max="2500" value="1500" data-ch="${ch}" style="--pct:50%">
          <button class="bs wig" onclick="wig(${ch})" title="Wiggle">W</button>
          <button class="bs rls" onclick="r(${ch})" title="Release">Rls</button>
        </div>
      </div>`;
    });
    card.innerHTML=h;
    card.querySelectorAll('input[type=range]').forEach(s=>{
      const ch=parseInt(s.dataset.ch);
      s.oninput=e=>{
        const v=parseInt(e.target.value);
        const pct=((v-500)/2000*100).toFixed(1)+'%';
        e.target.style.setProperty('--pct',pct);
        $('u'+ch).textContent=v+' µs';
        fetch(`/servo?ch=${ch}&us=${v}`);
      };
    });
    root.appendChild(card);
  });
}
build();

// Show camera stream section if /stream responds.
fetch('/stream',{method:'HEAD'}).then(r=>{ if(r.ok||r.status===200) $('stream-section').style.display=''; }).catch(()=>{});

function r(ch){fetch('/release?ch='+ch)}
function wig(ch){fetch('/wiggle?ch='+ch)}
function demo(){fetch('/demo_wave')}
function walk(){fetch('/walk')}
function releaseAll(){fetch('/release_all')}
function centerAll(){
  document.querySelectorAll('input[type=range]').forEach(s=>{
    s.value=1500; s.style.setProperty('--pct','50%');
    $('u'+s.dataset.ch).textContent='1500 µs';
    fetch(`/servo?ch=${s.dataset.ch}&us=1500`);
  });
}
function showHome(){
  fetch('/home').then(r=>r.json()).then(home=>{
    const lines=[];
    LEGS.forEach(L=>{
      const lbl=L.name.replace('Front-','F').replace('Back-','B').replace('Left','L').replace('Right','R');
      L.joints.forEach(({j:jname,ch})=>{
        const us=home[ch]!==undefined?home[ch]:1500;
        lines.push(`    /* ${lbl} ${jname.padEnd(5)} ch ${String(ch).padStart(2)} */ ${us},`);
      });
    });
    const txt='// Marvin home pulse-widths (paste into petbot.ino HOME_US[])\nstatic const uint16_t HOME_US[12] = {\n'+lines.join('\n')+'\n};\n';
    $('out').textContent=txt; $('out').style.display='block'; $('cpybtn').style.display='block';
  });
}
function copyOut(){
  navigator.clipboard.writeText($('out').textContent).then(
    ()=>{ $('cpybtn').textContent='✓ Copied!'; setTimeout(()=>$('cpybtn').textContent='Copy to clipboard',1500); },
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

// ─── Improved alternating-tripod walk gait ─────────────────────────────────

static inline uint16_t clamp_servo_pulse(int32_t us) {
    if (us < 400)  return 400;
    if (us > 2600) return 2600;
    return (uint16_t)us;
}

static inline bool leg_in_tripod(uint8_t leg, const uint8_t tripod[2]) {
    return leg == tripod[0] || leg == tripod[1];
}

//
// Fixes vs. the previous single-step version:
//
//  1. Per-leg hip direction sign (HIP_DIR[]).  Left/right servos are
//     mirror-mounted on the Freenove dog, so "forward" maps to opposite
//     µs directions.  Flip any entry in HIP_DIR[] if that leg walks
//     backward instead of forward.
//
//  2. Three distinct phases per half-cycle:
//       Phase 1 — LIFT:  raise active legs; stance legs adopt a slight
//                        crouch so the dog's weight stays centred.
//       Phase 2 — SWING: active hips swing forward; stance hips push
//                        backward (which moves the body forward).
//       Phase 3 — PLANT: lower active legs back to home height.
//
//  3. Both half-cycles (tripodA and tripodB) use the same positive
//     amplitude constants — the HIP_DIR[] sign handles direction.
//
static esp_err_t h_walk(httpd_req_t* r) {
    // Leg indices: 0=FL  1=FR  2=BL  3=BR
    // Tripod A: FL(0) + BR(3) — diagonal pair.
    // Tripod B: FR(1) + BL(2) — the other diagonal.
    const uint8_t tripodA[2] = { 0, 3 };
    const uint8_t tripodB[2] = { 1, 2 };

    // Hip forward direction per leg.
    // +1 : increasing µs swings the leg forward (left-side servos on this robot).
    // -1 : decreasing µs swings the leg forward (right-side servos are mirrored).
    // Flip any sign if the corresponding hip moves the wrong way after calibration.
    static const int8_t HIP_DIR[4] = { +1, -1, +1, -1 };  // FL, FR, BL, BR

    // ── Gait constants (µs offsets from the calibrated home position) ──
    static const int16_t SWING_HIP    = 120;  // forward swing amplitude (active leg)
    static const int16_t PUSH_HIP     =  80;  // backward push (stance leg / body advance)
    static const int16_t LIFT_THIGH   = -85;  // raise thigh to clear ground
    static const int16_t LIFT_CALF    = +130; // extend calf while thigh rises
    static const int16_t STANCE_THIGH = +30;  // slight stance crouch (stability)
    static const int16_t STANCE_CALF  = -40;
    static const uint16_t LIFT_MS  =  90;     // phase 1 duration
    static const uint16_t SWING_MS = 130;     // phase 2 duration
    static const uint16_t PLANT_MS =  80;     // phase 3 duration

    uint16_t hipHome[4], thighHome[4], calfHome[4];
    for (uint8_t leg = 0; leg < 4; leg++) {
        hipHome[leg]   = s_us[HIP_CH[leg]];
        thighHome[leg] = s_us[THIGH_CH[leg]];
        calfHome[leg]  = s_us[CALF_CH[leg]];
    }

    // One half-cycle: lift active tripod → swing all hips → plant active tripod.
    auto half_cycle = [&](const uint8_t active[2]) {

        // Phase 1 — lift active legs; stabilise stance legs.
        for (uint8_t leg = 0; leg < 4; leg++) {
            if (leg_in_tripod(leg, active)) {
                servo_set_us(THIGH_CH[leg], clamp_servo_pulse((int32_t)thighHome[leg] + LIFT_THIGH));
                servo_set_us(CALF_CH[leg],  clamp_servo_pulse((int32_t)calfHome[leg]  + LIFT_CALF));
            } else {
                servo_set_us(THIGH_CH[leg], clamp_servo_pulse((int32_t)thighHome[leg] + STANCE_THIGH));
                servo_set_us(CALF_CH[leg],  clamp_servo_pulse((int32_t)calfHome[leg]  + STANCE_CALF));
            }
        }
        delay(LIFT_MS);

        // Phase 2 — swing active hips forward; push stance hips backward.
        // HIP_DIR[] ensures each side moves in its correct physical direction.
        for (uint8_t leg = 0; leg < 4; leg++) {
            const int16_t hipDelta = leg_in_tripod(leg, active)
                ?  HIP_DIR[leg] * SWING_HIP    // active: swing forward
                : -HIP_DIR[leg] * PUSH_HIP;    // stance: push backward → body advances
            servo_set_us(HIP_CH[leg], clamp_servo_pulse((int32_t)hipHome[leg] + hipDelta));
        }
        delay(SWING_MS);

        // Phase 3 — plant active legs back at home height.
        for (uint8_t leg = 0; leg < 4; leg++) {
            if (leg_in_tripod(leg, active)) {
                servo_set_us(THIGH_CH[leg], thighHome[leg]);
                servo_set_us(CALF_CH[leg],  calfHome[leg]);
            }
        }
        delay(PLANT_MS);
    };

    Serial.println("[walk] cycle start");
    for (uint8_t i = 0; i < 3; i++) {
        half_cycle(tripodA);
        half_cycle(tripodB);
    }

    // Return all legs to home position.
    for (uint8_t leg = 0; leg < 4; leg++) {
        servo_set_us(HIP_CH[leg],   hipHome[leg]);
        servo_set_us(THIGH_CH[leg], thighHome[leg]);
        servo_set_us(CALF_CH[leg],  calfHome[leg]);
    }
    Serial.println("[walk] cycle end");
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

// ─── Camera init + MJPEG stream (only built with -DPETBOT_ENABLE_STREAM) ────
#ifdef PETBOT_ENABLE_STREAM

static void camera_init() {
    camera_config_t cfg = {};
    cfg.ledc_channel  = LEDC_CHANNEL_0;
    cfg.ledc_timer    = LEDC_TIMER_0;
    cfg.pin_d0        = CAM_PIN_D0;
    cfg.pin_d1        = CAM_PIN_D1;
    cfg.pin_d2        = CAM_PIN_D2;
    cfg.pin_d3        = CAM_PIN_D3;
    cfg.pin_d4        = CAM_PIN_D4;
    cfg.pin_d5        = CAM_PIN_D5;
    cfg.pin_d6        = CAM_PIN_D6;
    cfg.pin_d7        = CAM_PIN_D7;
    cfg.pin_xclk      = CAM_PIN_XCLK;
    cfg.pin_pclk      = CAM_PIN_PCLK;
    cfg.pin_vsync     = CAM_PIN_VSYNC;
    cfg.pin_href      = CAM_PIN_HREF;
    cfg.pin_sscb_sda  = CAM_PIN_SIOD;
    cfg.pin_sscb_scl  = CAM_PIN_SIOC;
    cfg.pin_pwdn      = CAM_PIN_PWDN;
    cfg.pin_reset     = CAM_PIN_RESET;
    cfg.xclk_freq_hz  = 20000000;
    cfg.pixel_format  = PIXFORMAT_JPEG;
    // QVGA (320×240) is a good balance: fast enough for live preview, small enough
    // for the ESP32's single-core HTTP server.  Bump to FRAMESIZE_VGA for better
    // detail at the cost of throughput.
    cfg.frame_size    = FRAMESIZE_QVGA;
    cfg.jpeg_quality  = 12;   // 0=best … 63=worst; 10-15 is a good range
    cfg.fb_count      = 2;    // double-buffer keeps the sensor fed

    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        Serial.printf("[cam] init FAILED 0x%x — check AI-Thinker pin map\n", err);
        return;
    }
    s_cam_ok = true;
    Serial.println("[cam] OV2640 ready — QVGA JPEG stream at http://192.168.4.1/stream");
}

// MJPEG multipart stream — keeps sending frames until the client disconnects.
static esp_err_t h_stream(httpd_req_t* r) {
    if (!s_cam_ok)
        return httpd_resp_send_err(r, HTTPD_503_SERVICE_UNAVAILABLE, "camera not ready");

    httpd_resp_set_type(r, "multipart/x-mixed-replace; boundary=PetBotFrame");
    httpd_resp_set_hdr(r, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store, no-cache");

    char part_hdr[80];
    esp_err_t res = ESP_OK;
    uint8_t fail_count = 0;
    while (res == ESP_OK) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("[cam] frame capture failed");
            if (++fail_count >= 10) {
                Serial.println("[cam] too many failures, closing stream");
                break;
            }
            delay(50);
            continue;
        }
        fail_count = 0;

        int hlen = snprintf(part_hdr, sizeof(part_hdr),
                            "\r\n--PetBotFrame\r\nContent-Type: image/jpeg\r\n"
                            "Content-Length: %zu\r\n\r\n", fb->len);
        res = httpd_resp_send_chunk(r, part_hdr, hlen);
        if (res == ESP_OK)
            res = httpd_resp_send_chunk(r, (const char*)fb->buf, fb->len);
        esp_camera_fb_return(fb);
    }
    // Zero-length chunk signals end of chunked response on disconnect.
    httpd_resp_send_chunk(r, nullptr, 0);
    return ESP_OK;
}

#endif  // PETBOT_ENABLE_STREAM

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

#ifdef PETBOT_ENABLE_STREAM
    camera_init();
#endif

    WiFi.mode(WIFI_AP);
    WiFi.softAP("PetBot_Cal", "petbot123");
    Serial.printf("[wifi] AP: PetBot_Cal  pw: petbot123  IP: %s\n",
                  WiFi.softAPIP().toString().c_str());

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 14;  // 8 base routes + /stream (optional) + head room
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

#ifdef PETBOT_ENABLE_STREAM
    if (s_cam_ok) {
        static const httpd_uri_t stream_route = { "/stream", HTTP_GET, h_stream, nullptr };
        httpd_register_uri_handler(s_httpd, &stream_route);
        Serial.println("[http] stream at http://192.168.4.1/stream");
    }
#endif

    Serial.println("[http] up — open http://192.168.4.1");
}

void loop() {
    delay(100);
}
