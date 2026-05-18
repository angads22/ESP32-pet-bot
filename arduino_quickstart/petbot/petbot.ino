/*
 *  PetBot — Phase 1.2 single-file Arduino sketch
 *  ----------------------------------------------
 *  Board     : Freenove ESP32-WROVER CAM (classic ESP32, OV2640/OV3660)
 *
 *  What's new vs Phase 1.1:
 *    - PCA9685 servo driver on I2C (SDA=GPIO13, SCL=GPIO14)
 *    - 12 leg-servo sliders + "Release" + "Save Home" (NVS)
 *    - Virtual joystick (Move tab; gait engine wires up in Phase 1.3)
 *    - WiFi mode toggle: AP (default) or join home WiFi
 *    - Hold GPIO 0 (BOOT button) low for 3 s at boot → wipe WiFi config
 *    - Better tabbed UI, "camera offline" placeholder if init fails
 *
 *  ── Required libraries (Tools → Manage Libraries…) ─────────────────────
 *    - Adafruit PWM Servo Driver Library    by Adafruit
 *      (pulls in Adafruit BusIO automatically)
 *
 *  ── Tools menu (classic Freenove ESP32-WROVER CAM) ─────────────────────
 *    Board                  : AI Thinker ESP32-CAM
 *    Flash Mode             : QIO
 *    Flash Frequency        : 80MHz
 *    Partition Scheme       : Huge APP (3MB No OTA/1MB SPIFFS)
 *    PSRAM                  : Enabled    (NOT "OPI PSRAM")
 *    Upload Speed           : 921600
 *
 *  If "Camera not supported" still appears after updating to v3.x of the
 *  esp32 board package, your camera module isn't OV2640/OV3660/OV5640 —
 *  paste the chip label and I'll look it up. The rest of the sketch
 *  (WiFi, servos, calibration) keeps working even if the camera fails.
 *
 *  ── Servo wiring (do this before powering up servos!) ──────────────────
 *    PCA9685 VCC    ←  3.3V  from ESP32-CAM (logic side)
 *    PCA9685 GND    ←  GND   from ESP32-CAM
 *    PCA9685 SDA    ←  GPIO 13 on ESP32-CAM
 *    PCA9685 SCL    ←  GPIO 14 on ESP32-CAM
 *    PCA9685 V+     ←  5–6 V regulated supply  (NOT raw 11.1V battery!)
 *    PCA9685 GND    ←  battery GND
 *    Common ground tied: ESP32-CAM GND ↔ battery GND ↔ PCA9685 GND
 *
 *  Servos in Freenove default channel order:
 *    FL hip=0   FL thigh=1   FL calf=2
 *    FR hip=5   FR thigh=6   FR calf=7
 *    BL hip=8   BL thigh=9   BL calf=10
 *    BR hip=13  BR thigh=14  BR calf=15   (channels 3, 4, 11, 12 unused)
 *
 *  ── First-time boot flow ───────────────────────────────────────────────
 *    1. Bot broadcasts WiFi `PetBot_xxxx` (password petbot123)
 *    2. Phone → join that WiFi → open http://192.168.4.1
 *    3. Calibrate tab → slide each servo until the dog stands cleanly
 *    4. Save Home
 *    5. Settings tab → optionally switch to "Join home WiFi"
 *
 *  To reset WiFi to AP mode (if home-WiFi join fails):
 *    Power off → hold BOOT button → power on → keep holding 3 seconds
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "esp_http_server.h"
#include "esp_camera.h"
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

// ─── Camera pin map: Freenove ESP32-WROVER CAM / AI-Thinker ESP32-CAM ───
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

// ─── I2C → PCA9685 ──────────────────────────────────────────────────────
#define I2C_SDA       13
#define I2C_SCL       14
#define PCA9685_ADDR  0x40

// ─── Servo layout ───────────────────────────────────────────────────────
// PCA9685 channel → physical leg+joint mapping for THIS user's robot.
// Grouped FL, FR, BL, BR with [hip, thigh, calf] inside each leg, so
// the gait engine can index as LEG_CH[leg*3 + joint]:
//   leg 0 = FL,  leg 1 = FR,  leg 2 = BL,  leg 3 = BR
//   joint 0 = hip, joint 1 = thigh, joint 2 = calf
const uint8_t LEG_CH[12]   = {
     3,  1,  2,   // FL: hip=ch3,  thigh=ch1,  calf=ch2   (LF hip moved from ch0 → ch3)
    15, 14, 13,   // FR: hip=ch15, thigh=ch14, calf=ch13  (reversed in channel order)
     7,  6,  5,   // BL: hip=ch7,  thigh=ch6,  calf=ch5   (reversed in channel order)
     8,  9, 10,   // BR: hip=ch8,  thigh=ch9,  calf=ch10
};
const char*   LEG_NAME[12] = {
    "FL hip", "FL thigh", "FL calf",
    "FR hip", "FR thigh", "FR calf",
    "BL hip", "BL thigh", "BL calf",
    "BR hip", "BR thigh", "BR calf",
};

// ─── Default home positions (calibrated per-bot) ────────────────────────
// Replace these with the output of petbot_calibrate.ino's
// "Show home values" button. The NVS-saved values still take priority
// once the user has hit "Save Home" in the app — these are the bootstrap
// fallback before any save has happened (e.g. after a fresh flash).
static const uint16_t HOME_US[12] = {
    /* FL hip   ch  3 */ 1500,
    /* FL thigh ch  1 */ 1500,
    /* FL calf  ch  2 */ 1500,
    /* FR hip   ch 15 */ 1500,
    /* FR thigh ch 14 */ 1500,
    /* FR calf  ch 13 */ 1500,
    /* BL hip   ch  7 */ 1500,
    /* BL thigh ch  6 */ 1500,
    /* BL calf  ch  5 */ 1500,
    /* BR hip   ch  8 */ 1500,
    /* BR thigh ch  9 */ 1500,
    /* BR calf  ch 10 */ 1500,
};

// ─── Globals ────────────────────────────────────────────────────────────
static Adafruit_PWMServoDriver s_pca(PCA9685_ADDR);
static Preferences             s_prefs;
static uint16_t                s_servo_us[16] = {0};
static uint16_t                s_home_us[16]  = {0};
static bool                    s_pca_ok       = false;
static bool                    s_camera_ok    = false;
static httpd_handle_t          s_httpd        = nullptr;
static char                    s_ap_ssid[32]  = {0};
static String                  s_home_ssid    = "";
static String                  s_home_pass    = "";
static uint8_t                 s_wifi_mode    = 0;  // 0 = AP, 1 = STA
static String                  s_active_ip    = "";
static String                  s_active_mode  = "AP";

// ─── Servo helpers ──────────────────────────────────────────────────────
static inline uint16_t us_to_ticks(uint16_t us) {
    return (uint32_t)us * 4096UL / 20000UL;   // 50 Hz → 20 ms → 4096 ticks
}

static void servo_set_us(uint8_t ch, uint16_t us) {
    if (us < 400)  us = 400;
    if (us > 2600) us = 2600;
    s_servo_us[ch] = us;
    if (s_pca_ok) s_pca.setPWM(ch, 0, us_to_ticks(us));
}

static void servo_release(uint8_t ch) {
    if (s_pca_ok) s_pca.setPWM(ch, 0, 4096);  // full-off
}

static void all_release() {
    for (uint8_t i = 0; i < 12; i++) servo_release(LEG_CH[i]);
}

static void all_to_home() {
    for (uint8_t i = 0; i < 12; i++) servo_set_us(LEG_CH[i], s_home_us[LEG_CH[i]]);
}

// ─── NVS ────────────────────────────────────────────────────────────────
static uint16_t home_default_for(uint8_t ch) {
    // Look up the calibrated default for this PCA9685 channel, falling
    // back to 1500 µs (neutral) for unused channels (3, 4, 11, 12).
    for (uint8_t i = 0; i < 12; i++) if (LEG_CH[i] == ch) return HOME_US[i];
    return 1500;
}

static void load_home() {
    s_prefs.begin("home", true);
    for (uint8_t ch = 0; ch < 16; ch++) {
        char k[6]; snprintf(k, sizeof(k), "c%u", ch);
        s_home_us[ch]  = s_prefs.getUShort(k, home_default_for(ch));
        s_servo_us[ch] = s_home_us[ch];
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

// ─── Web app (tabbed UI) ────────────────────────────────────────────────
static const char WEBAPP_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>PetBot</title>
<style>
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent;font-family:system-ui,sans-serif}
body{background:#0a0a14;color:#e7e9ee;margin:0;padding:0;max-width:560px;margin:0 auto}
header{padding:14px 16px;display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid #1f2236}
h1{margin:0;font-size:18px;color:#e94560;letter-spacing:.04em}
.pill{font:11px ui-monospace,monospace;padding:4px 8px;background:#16213e;border:1px solid #1f2236;border-radius:12px;color:#9aa0b4}
nav.tabs{display:flex;gap:0;border-bottom:1px solid #1f2236}
nav.tabs button{flex:1;padding:12px;background:transparent;color:#9aa0b4;border:none;border-bottom:2px solid transparent;font-size:14px;cursor:pointer}
nav.tabs button.active{color:#e94560;border-bottom-color:#e94560}
.tab{display:none;padding:16px}
.tab.active{display:block}
.cam-wrap{position:relative;border-radius:8px;overflow:hidden;background:#16213e;aspect-ratio:4/3;margin-bottom:14px}
#cam{width:100%;display:block}
#cam-off{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;color:#9aa0b4;font-size:13px}
.joy{margin:16px auto;width:200px;height:200px;border-radius:50%;background:radial-gradient(#16213e 50%,#101428 100%);border:2px solid #1f2236;position:relative;touch-action:none}
.nub{position:absolute;top:50%;left:50%;width:60px;height:60px;margin-left:-30px;margin-top:-30px;border-radius:50%;background:#e94560;transition:transform .08s ease-out;pointer-events:none}
.row{display:flex;align-items:center;gap:8px;margin:8px 0}
.row label{font-size:13px;flex:0 0 80px;color:#9aa0b4}
.row input[type=range]{flex:1;accent-color:#e94560}
.row .us{font:12px ui-monospace,monospace;color:#9aa0b4;min-width:40px;text-align:right}
.row button{padding:6px 10px;background:#16213e;color:#fff;border:1px solid #1f2236;border-radius:6px;font-size:12px;cursor:pointer}
.row button:active{background:#e94560}
.leg-group{margin-bottom:14px;padding:10px;background:#0f1322;border:1px solid #1f2236;border-radius:8px}
.leg-group h3{margin:0 0 6px;font-size:12px;color:#e94560;text-transform:uppercase;letter-spacing:.06em}
.actions{display:flex;flex-wrap:wrap;gap:8px;margin-top:14px}
.actions button{padding:12px 16px;background:#16213e;color:#fff;border:2px solid #1f2236;border-radius:8px;font-size:14px;cursor:pointer;flex:1;min-width:100px}
.actions button:active{background:#e94560}
.actions button.primary{background:#e94560;border-color:#e94560}
.hint{font-size:12px;color:#9aa0b4;line-height:1.5}
input[type=text],input[type=password]{width:100%;padding:10px;background:#16213e;color:#fff;border:1px solid #1f2236;border-radius:6px;font-size:14px;margin-top:6px}
.radio{display:block;padding:10px;background:#16213e;border:1px solid #1f2236;border-radius:6px;margin:6px 0;cursor:pointer}
.radio input{margin-right:8px;accent-color:#e94560}
</style></head><body>
<header><h1>PetBot</h1><div class="pill" id="status">…</div></header>

<nav class="tabs">
  <button data-tab="move" class="active">Move</button>
  <button data-tab="calib">Calibrate</button>
  <button data-tab="set">Settings</button>
</nav>

<section id="tab-move" class="tab active">
  <div class="cam-wrap"><img id="cam" src="/stream" onerror="camOff()"><div id="cam-off" style="display:none">camera offline</div></div>
  <div class="joy" id="jb"><div class="nub" id="jn"></div></div>
  <div class="row"><label>Speed</label><input type="range" id="speed" min="1" max="8" value="3"><span class="us" id="speed-v">3</span></div>
  <div class="actions">
    <button onclick="c('STAND')">Stand</button>
    <button onclick="c('SIT')">Sit</button>
    <button onclick="c('HOME')">Home</button>
  </div>
  <h3 style="margin:18px 0 6px;font-size:12px;color:#e94560;text-transform:uppercase;letter-spacing:.06em">Face</h3>
  <div class="actions">
    <button onclick="f('IDLE')">(·_·) idle</button>
    <button onclick="f('HAPPY')">(^ω^) happy</button>
    <button onclick="f('WALK')">walking</button>
    <button onclick="f('SAD')">(︶︹︶) sad</button>
    <button onclick="f('CRY')">(T_T) cry</button>
    <button onclick="f('ANGRY')">(ಠ益ಠ) angry</button>
    <button onclick="f('LOVE')">(♡_♡) love</button>
    <button onclick="f('SLEEP')">(=_=) sleep</button>
    <button onclick="f('SEARCH')">(•_•) search</button>
    <button onclick="f('CURIOUS')">(?_?) curious</button>
    <button onclick="f('TABLE_FLIP')">┻━┻ flip</button>
  </div>
</section>

<section id="tab-calib" class="tab">
  <p class="hint">Slide each servo until the dog is in a clean standing pose. Tap <b>Release</b> to make a leg limp — move it with your hand, slide back to where it should sit. When the whole dog is right, tap <b>Save Home</b>.</p>
  <div id="legs"></div>
  <div class="actions">
    <button class="primary" onclick="saveHome()">Save Home</button>
    <button onclick="releaseAll()">Release All</button>
    <button onclick="goHome()">Reset to saved</button>
  </div>
</section>

<section id="tab-set" class="tab">
  <p class="hint">Switch how you reach your bot. AP mode = bot broadcasts its own WiFi. Home mode = bot joins your home network and is reachable at <code>http://petbot.local</code>.</p>
  <label class="radio"><input type="radio" name="wm" value="0"> <b>AP mode</b> — broadcast PetBot_xxxx</label>
  <label class="radio"><input type="radio" name="wm" value="1"> <b>Home WiFi</b> — join an existing network</label>
  <div id="creds">
    <input type="text" id="ssid" placeholder="Home WiFi SSID" autocomplete="off">
    <input type="password" id="pass" placeholder="Password" autocomplete="off">
  </div>
  <div class="actions">
    <button class="primary" onclick="applyWiFi()">Apply &amp; Reboot</button>
  </div>
  <p class="hint" style="margin-top:18px">Stuck in home-WiFi mode and can't reach the bot? Power off, hold the <b>BOOT</b> button, power back on, keep holding 3 s → wipes WiFi config, falls back to AP.</p>
</section>

<script>
const $=id=>document.getElementById(id);

// Tabs
document.querySelectorAll('nav.tabs button').forEach(b=>{
  b.onclick=()=>{
    document.querySelectorAll('nav.tabs button').forEach(x=>x.classList.remove('active'));
    document.querySelectorAll('.tab').forEach(x=>x.classList.remove('active'));
    b.classList.add('active');
    $('tab-'+b.dataset.tab).classList.add('active');
  };
});

// Camera fallback
function camOff(){ $('cam').style.display='none'; $('cam-off').style.display='flex'; }

// HTTP helpers
function fetchTxt(url){return fetch(url).then(r=>r.text())}
function c(cmd){return fetchTxt('/cmd?c='+encodeURIComponent(cmd))}
function f(face){return fetchTxt('/face?n='+encodeURIComponent(face))}

// Status pill
function refreshStatus(){
  fetchTxt('/status').then(t=>$('status').textContent=t).catch(()=>$('status').textContent='offline');
}
setInterval(refreshStatus, 2000);
refreshStatus();

// Calibrate tab — build 12 sliders grouped by leg
const LEGS=[
  ['Front-Left',  [{n:'hip',ch:0},{n:'thigh',ch:1},{n:'calf',ch:2}]],
  ['Front-Right', [{n:'hip',ch:5},{n:'thigh',ch:6},{n:'calf',ch:7}]],
  ['Back-Left',   [{n:'hip',ch:8},{n:'thigh',ch:9},{n:'calf',ch:10}]],
  ['Back-Right',  [{n:'hip',ch:13},{n:'thigh',ch:14},{n:'calf',ch:15}]],
];

function buildLegs(){
  const root=$('legs'); root.innerHTML='';
  LEGS.forEach(([lname, joints])=>{
    const g=document.createElement('div'); g.className='leg-group';
    g.innerHTML=`<h3>${lname}</h3>`;
    joints.forEach(j=>{
      const row=document.createElement('div'); row.className='row';
      row.innerHTML=`
        <label>${j.n}</label>
        <input type="range" min="500" max="2500" value="1500" data-ch="${j.ch}">
        <span class="us" id="u${j.ch}">1500</span>
        <button onclick="r(${j.ch})">Release</button>`;
      const slider=row.querySelector('input');
      slider.oninput=e=>{
        const v=parseInt(e.target.value);
        $('u'+j.ch).textContent=v;
        fetchTxt(`/servo?ch=${j.ch}&us=${v}`);
      };
      g.appendChild(row);
    });
    root.appendChild(g);
  });
  // Pull saved positions into the sliders
  fetchTxt('/home').then(t=>{
    const m=t.split(',').map(x=>x.split('=').map(y=>parseInt(y)));
    m.forEach(([ch,us])=>{
      const s=document.querySelector(`input[data-ch="${ch}"]`);
      if(s){ s.value=us; $('u'+ch).textContent=us; }
    });
  }).catch(()=>{});
}
buildLegs();

function r(ch){fetchTxt('/release?ch='+ch)}
function releaseAll(){fetchTxt('/release_all')}
function saveHome(){fetchTxt('/save_home').then(t=>alert(t))}
function goHome(){fetchTxt('/load_home').then(()=>buildLegs())}

// Settings tab
function applyWiFi(){
  const m=document.querySelector('input[name=wm]:checked');
  if(!m){alert('Pick a mode'); return;}
  const params=new URLSearchParams({mode:m.value, ssid:$('ssid').value, pass:$('pass').value});
  fetchTxt('/set_wifi?'+params).then(t=>alert(t));
}

// Joystick
const stick={base:$('jb'),nub:$('jn'),active:false,cx:0,cy:0,r:80,last:0,x:0,y:0};
function jStart(e){
  e.preventDefault();
  const r=stick.base.getBoundingClientRect();
  stick.cx=r.left+r.width/2; stick.cy=r.top+r.height/2; stick.active=true;
  jMove(e);
}
function jMove(e){
  if(!stick.active) return;
  e.preventDefault();
  const t=e.touches?e.touches[0]:e;
  let dx=t.clientX-stick.cx, dy=t.clientY-stick.cy;
  const d=Math.hypot(dx,dy);
  if(d>stick.r){dx=dx*stick.r/d; dy=dy*stick.r/d;}
  stick.nub.style.transform=`translate(${dx}px,${dy}px)`;
  stick.x=dx/stick.r; stick.y=-dy/stick.r;
  jSend();
}
function jEnd(e){
  if(!stick.active) return;
  stick.active=false; stick.x=stick.y=0;
  stick.nub.style.transform='translate(0,0)';
  fetchTxt('/joy?x=0&y=0');
}
function jSend(){
  const now=Date.now();
  if(now-stick.last<100) return;
  stick.last=now;
  fetchTxt(`/joy?x=${stick.x.toFixed(2)}&y=${stick.y.toFixed(2)}`);
}
stick.base.addEventListener('mousedown',jStart);
stick.base.addEventListener('touchstart',jStart,{passive:false});
window.addEventListener('mousemove',jMove);
window.addEventListener('touchmove',jMove,{passive:false});
window.addEventListener('mouseup',jEnd);
window.addEventListener('touchend',jEnd);

// Speed slider readout
$('speed').oninput=e=>$('speed-v').textContent=e.target.value;
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

static bool get_query_int(const char* q, const char* key, int* out) {
    char buf[24] = {};
    if (httpd_query_key_value(q, key, buf, sizeof(buf)) != ESP_OK) return false;
    *out = atoi(buf);
    return true;
}

static esp_err_t handle_cmd(httpd_req_t* r) {
    char q[80] = {};
    char reply[64] = "ok";
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK) {
        char raw[48] = {}, dec[48] = {};
        if (httpd_query_key_value(q, "c", raw, sizeof(raw)) == ESP_OK) {
            url_decode(dec, raw, sizeof(dec));
            Serial.print("[cmd] "); Serial.println(dec);
            if (strcmp(dec, "HOME") == 0) {
                all_to_home();
                snprintf(reply, sizeof(reply), "ok:home");
            } else if (strcmp(dec, "STAND") == 0 || strcmp(dec, "SIT") == 0) {
                snprintf(reply, sizeof(reply), "todo:%s", dec);  // gait engine = Step 1.3
            } else {
                snprintf(reply, sizeof(reply), "ok:%s", dec);
            }
        }
    }
    httpd_resp_sendstr(r, reply);
    return ESP_OK;
}

static esp_err_t handle_servo(httpd_req_t* r) {
    char q[64] = {};
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) != ESP_OK)
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "no query");
    int ch, us;
    if (!get_query_int(q, "ch", &ch) || !get_query_int(q, "us", &us))
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "need ch,us");
    if (ch < 0 || ch > 15) return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "bad ch");
    servo_set_us((uint8_t)ch, (uint16_t)us);
    httpd_resp_sendstr(r, "ok");
    return ESP_OK;
}

static esp_err_t handle_release(httpd_req_t* r) {
    char q[32] = {};
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK) {
        int ch;
        if (get_query_int(q, "ch", &ch) && ch >= 0 && ch <= 15) {
            servo_release((uint8_t)ch);
        }
    }
    httpd_resp_sendstr(r, "ok");
    return ESP_OK;
}

static esp_err_t handle_release_all(httpd_req_t* r) {
    all_release();
    httpd_resp_sendstr(r, "ok:released");
    return ESP_OK;
}

static esp_err_t handle_save_home(httpd_req_t* r) {
    save_home();
    httpd_resp_sendstr(r, "Home position saved.");
    return ESP_OK;
}

static esp_err_t handle_load_home(httpd_req_t* r) {
    load_home();
    all_to_home();
    httpd_resp_sendstr(r, "ok:loaded");
    return ESP_OK;
}

static esp_err_t handle_home(httpd_req_t* r) {
    char buf[256] = {};
    int n = 0;
    for (uint8_t i = 0; i < 12; i++) {
        n += snprintf(buf + n, sizeof(buf) - n, "%s%u=%u",
                      i ? "," : "", LEG_CH[i], s_home_us[LEG_CH[i]]);
    }
    httpd_resp_sendstr(r, buf);
    return ESP_OK;
}

// Emit a FACE:NAME command to the C6 head display. Today it prints to
// Serial (so you can see it in the Arduino IDE monitor and paste it to
// the C6's serial monitor for testing). When you wire the bot's UART
// TX to the C6's Serial1 RX (3 wires + GND), also call Serial2.println
// here — Serial2 begin() in setup() with appropriate free GPIOs.
static esp_err_t handle_face(httpd_req_t* r) {
    char q[64] = {};
    char nameBuf[24] = {};
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK)
        httpd_query_key_value(q, "n", nameBuf, sizeof(nameBuf));
    if (!nameBuf[0]) return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "need n=NAME");
    // Send the line in the format the C6 sketch expects.
    Serial.print("FACE:"); Serial.println(nameBuf);
    // Serial2.print("FACE:"); Serial2.println(nameBuf);   // enable after wiring
    char reply[40];
    snprintf(reply, sizeof(reply), "ok face=%s", nameBuf);
    httpd_resp_sendstr(r, reply);
    return ESP_OK;
}

static esp_err_t handle_status(httpd_req_t* r) {
    char buf[160] = {};
    snprintf(buf, sizeof(buf),
             "%s @ %s | cam=%s pca=%s | up=%lus",
             s_active_mode.c_str(), s_active_ip.c_str(),
             s_camera_ok ? "ok" : "off",
             s_pca_ok ? "ok" : "off",
             (unsigned long)(millis() / 1000));
    httpd_resp_sendstr(r, buf);
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
    httpd_resp_sendstr(r, "Saved. Rebooting in 2 s…");
    delay(2000);
    ESP.restart();
    return ESP_OK;
}

static esp_err_t handle_stream(httpd_req_t* req) {
    if (!s_camera_ok) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no camera");
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

// ─── Bring-up ───────────────────────────────────────────────────────────
static bool init_camera() {
    camera_config_t cfg = {};
    cfg.ledc_channel = LEDC_CHANNEL_0; cfg.ledc_timer = LEDC_TIMER_0;
    cfg.pin_d0 = CAM_D0; cfg.pin_d1 = CAM_D1; cfg.pin_d2 = CAM_D2;
    cfg.pin_d3 = CAM_D3; cfg.pin_d4 = CAM_D4; cfg.pin_d5 = CAM_D5;
    cfg.pin_d6 = CAM_D6; cfg.pin_d7 = CAM_D7;
    cfg.pin_xclk = CAM_XCLK; cfg.pin_pclk = CAM_PCLK;
    cfg.pin_vsync = CAM_VSYNC; cfg.pin_href = CAM_HREF;
    cfg.pin_sscb_sda = CAM_SIOD; cfg.pin_sscb_scl = CAM_SIOC;
    cfg.pin_pwdn = CAM_PWDN;  cfg.pin_reset = CAM_RESET;
    cfg.xclk_freq_hz = 20000000;
    cfg.pixel_format = PIXFORMAT_JPEG;
    cfg.frame_size   = FRAMESIZE_QVGA;
    cfg.jpeg_quality = 12;
    cfg.fb_count     = 2;
    if (esp_camera_init(&cfg) != ESP_OK) {
        Serial.println("[cam] init FAILED — sketch continues without camera");
        return false;
    }
    Serial.println("[cam] OV2640/OV3660 ready");
    return true;
}

static bool init_pca9685() {
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);
    if (!s_pca.begin()) {
        Serial.println("[pca] begin FAILED — sketch continues without servos");
        return false;
    }
    s_pca.setOscillatorFrequency(27000000);
    s_pca.setPWMFreq(50);
    Serial.println("[pca] PCA9685 ready @ 0x40, 50 Hz");
    // Drive each leg servo to its saved home pulse-width
    for (uint8_t i = 0; i < 12; i++) {
        servo_set_us(LEG_CH[i], s_home_us[LEG_CH[i]]);
        delay(15);
    }
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
    s_active_ip = WiFi.localIP().toString();
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
    s_active_ip = WiFi.softAPIP().toString();
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
    cfg.max_uri_handlers = 16;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        Serial.println("[http] httpd_start FAILED");
        return;
    }
    static const httpd_uri_t routes[] = {
        { "/",            HTTP_GET, handle_root,         nullptr },
        { "/stream",      HTTP_GET, handle_stream,       nullptr },
        { "/cmd",         HTTP_GET, handle_cmd,          nullptr },
        { "/servo",       HTTP_GET, handle_servo,        nullptr },
        { "/release",     HTTP_GET, handle_release,      nullptr },
        { "/release_all", HTTP_GET, handle_release_all,  nullptr },
        { "/save_home",   HTTP_GET, handle_save_home,    nullptr },
        { "/load_home",   HTTP_GET, handle_load_home,    nullptr },
        { "/home",        HTTP_GET, handle_home,         nullptr },
        { "/status",      HTTP_GET, handle_status,       nullptr },
        { "/face",        HTTP_GET, handle_face,         nullptr },
        { "/set_wifi",    HTTP_GET, handle_set_wifi,     nullptr },
        { "/joy",         HTTP_GET, handle_cmd,          nullptr },  // accepted; gait wires in 1.3
    };
    for (auto& u : routes) httpd_register_uri_handler(s_httpd, &u);
    Serial.println("[http] up");
}

// ─── BOOT-button hold-3s wipes WiFi config ──────────────────────────────
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
    } else {
        Serial.println("[wifi] BOOT released early — skipping wipe");
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=== PetBot Phase 1.2 booting ===");

    maybe_reset_wifi_cfg();
    load_home();
    load_wifi_cfg();

    s_camera_ok = init_camera();
    s_pca_ok    = init_pca9685();
    init_wifi();
    init_http();

    Serial.println("=== PetBot ready ===");
}

void loop() {
    // HTTP runs in its own task. Gait + sensors land in Step 1.3.
    delay(100);
}
