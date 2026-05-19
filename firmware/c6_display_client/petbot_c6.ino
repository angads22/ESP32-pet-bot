/*
 *  PetBot / Marvin — C6 head-display sketch
 *  ─────────────────────────────────────────
 *  Board     : Waveshare ESP32-C6-LCD-1.47 (onboard ST7789 172×320)
 *  Role      : Animated kaomoji-style face for Marvin. Listens on Serial
 *              for FACE:NAME commands and renders the matching emotion.
 *
 *  ── Faces (drawn as graphic primitives — vibe of the kaomoji) ─────────
 *    FACE:IDLE       (·_·)            neutral; glances around + blink
 *    FACE:HAPPY      (^ω^)            caret eyes, omega mouth, blush
 *    FACE:SAD        (︶︹︶)         arc-down eyes, frown mouth
 *    FACE:CRY        (T_T)            T-shaped eyes, tears falling
 *    FACE:ANGRY      (ಠ益ಠ)         glare eyes + brow, gritted teeth
 *    FACE:LOVE       (♡μ_μ)          heart eyes, sparkles
 *    FACE:SLEEP      (=_=) zZz       closed eyes, sleeping Zs
 *    FACE:SEARCH     (•_•)            wide eyes with pupils panning
 *    FACE:CURIOUS    (?_?)            ringed eyes with floating "?"
 *    FACE:WALK                        bouncing eyes, slight grin
 *    FACE:TABLE_FLIP (ノಠ益ಠ)ノ彡┻━┻  arms up, motion lines, flying table
 *    FACE:BLINK                       one-shot blink, returns to current
 *    PING                             replies "pong" — link healthcheck
 *
 *  Auto-animations while a face is held:
 *    - IDLE / HAPPY / SAD / CURIOUS: pupils glance around every 3.5–7 s
 *      + a random 130 ms blink every 2.5–4.5 s.
 *    - SEARCH: pupils pan ±18 px every ~110 ms.
 *    - WALK:   vertical bounce ±4 px at 5 Hz.
 *    - SLEEP:  static (no animation; gentle by design).
 *    - TABLE_FLIP: static (the whole face is the punchline).
 *
 *  ── How to test now (no bot wiring required) ──────────────────────────
 *    Flash via the C6's USB-C, open Serial Monitor at 115200,
 *    type     FACE:HAPPY        + Enter, screen changes.
 *    Type     FACE:TABLE_FLIP   for the chaos one.
 *
 *  ── How to wire bot → C6 later (proper link) ──────────────────────────
 *    Body GPIO 4  (TX)   →   C6 GPIO 16 (Serial1 RX)
 *    Body GND            →   C6 GND
 *    Uncomment the Serial1.begin(...) + pump_serial(Serial1) calls below.
 *
 *  ── Arduino IDE setup ─────────────────────────────────────────────────
 *    Tools → Board → ESP32 Arduino → "ESP32C6 Dev Module"
 *    Tools → USB CDC On Boot      → Enabled
 *    Tools → Partition Scheme     → Default 4MB
 *    Tools → Upload Speed         → 921600
 *
 *  ── Libraries (install via Tools → Manage Libraries) ──────────────────
 *    - Adafruit GFX Library                  by Adafruit
 *    - Adafruit ST7735 and ST7789 Library    by Adafruit
 */

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// ─── ST7789 wiring on the C6-LCD-1.47 (board-fixed, do not change) ──────
#define TFT_MOSI   6
#define TFT_SCLK   7
#define TFT_CS    14
#define TFT_DC    15
#define TFT_RST   21
#define TFT_BL    22

#define SCR_W    320
#define SCR_H    172

static Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, TFT_CS, TFT_DC, TFT_RST);

// ─── Face state ─────────────────────────────────────────────────────────
enum FaceMode : uint8_t {
    F_IDLE = 0, F_HAPPY, F_SAD, F_CRY, F_ANGRY, F_LOVE,
    F_SLEEP, F_SEARCH, F_CURIOUS, F_WALK, F_TABLE_FLIP,
};

static FaceMode  s_face        = F_IDLE;
static bool      s_blink_on    = false;
static uint32_t  s_blink_until = 0;
static uint32_t  s_next_blink  = 0;

// Idle-glance animation
static int8_t    s_glance_x      = 0;
static int8_t    s_glance_y      = 0;
static uint32_t  s_next_glance   = 0;
static uint32_t  s_glance_clear  = 0;

// Search pan
static int8_t    s_search_dir  = 1;
static uint32_t  s_next_search = 0;
static int8_t    s_search_off  = 0;

// Walk bounce
static constexpr uint8_t WALK_PHASE_COUNT = 4;
static uint8_t   s_walk_phase  = 0;
static uint32_t  s_next_walk   = 0;

static String    s_buf;

// Useful colors (RGB565)
#define C_WHITE   0xFFFF
#define C_BLACK   0x0000
#define C_RED     0xF800
#define C_PINK    0xFA1F
#define C_BLUE    0x041F
#define C_TEAL    0x07FF
#define C_YELLOW  0xFFE0
#define C_BROWN   0xA200
#define C_ORANGE  0xFD20
#define C_GRAY    0x4208

// Eye positions (centers)
#define EYE_L_X   100
#define EYE_R_X   (SCR_W - 100)
#define EYE_Y      80

// ─── Eye primitives ─────────────────────────────────────────────────────
// (^ ^) caret peaks — two diagonal lines meeting at a top point.
static void eye_caret(int cx, int cy, int w = 30, int h = 14, uint16_t c = C_WHITE) {
    int x0 = cx - w / 2, x1 = cx + w / 2;
    int yb = cy + h / 2, yt = cy - h / 2;
    for (int t = 0; t < 3; t++) {
        tft.drawLine(x0, yb + t, cx, yt + t, c);
        tft.drawLine(cx, yt + t, x1, yb + t, c);
    }
}

// (︶ ︶) downward arc — open-side-up smile-like shape, but for eyes.
static void eye_arc_down(int cx, int cy, int w = 32, int h = 10, uint16_t c = C_WHITE) {
    for (int dx = -w / 2; dx <= w / 2; dx++) {
        float t = (float)dx / (w / 2.0f);
        int dy = (int)(h * (1.0f - t * t));
        for (int s = 0; s < 2; s++) tft.drawPixel(cx + dx, cy + dy - s, c);
    }
}

// (T T) — thick T-shape for crying eyes.
static void eye_t(int cx, int cy, uint16_t c = C_WHITE) {
    tft.fillRect(cx - 15, cy - 12, 30, 4, c);
    tft.fillRect(cx - 2,  cy - 12, 4,  24, c);
}

// (♡ ♡) — heart eye.
static void eye_heart(int cx, int cy, uint16_t c = C_PINK) {
    tft.fillCircle(cx - 8, cy - 4, 10, c);
    tft.fillCircle(cx + 8, cy - 4, 10, c);
    tft.fillTriangle(cx - 17, cy + 1, cx + 17, cy + 1, cx, cy + 18, c);
}

// (ಠ ಠ) — circular glare eye with thick angry brow.
static void eye_glare(int cx, int cy, uint16_t c = C_WHITE) {
    tft.fillCircle(cx, cy + 2, 14, c);
    tft.fillCircle(cx, cy + 2, 5, C_BLACK);
    // Thick angled brow on top, red
    for (int t = 0; t < 5; t++) {
        tft.drawLine(cx - 20, cy - 18 + t, cx + 16, cy - 24 + t, C_RED);
    }
}

// (=_=) closed-eye line.
static void eye_closed(int cx, int cy, int w = 30, uint16_t c = C_WHITE) {
    tft.fillRect(cx - w / 2, cy - 2, w, 4, c);
}

// (•_•) plain wide eye, with pupil offset.
static void eye_dot(int cx, int cy, int pupil_x = 0, int pupil_y = 0,
                    uint16_t eye_c = C_WHITE, uint16_t pup_c = C_BLACK) {
    tft.fillCircle(cx, cy, 16, eye_c);
    tft.fillCircle(cx + pupil_x, cy + pupil_y, 6, pup_c);
}

// (?_?) — circle outline + floating "?".
static void eye_question(int cx, int cy, uint16_t c = C_WHITE) {
    tft.drawCircle(cx, cy, 14, c);
    tft.drawCircle(cx, cy, 13, c);
    tft.fillCircle(cx, cy, 4, c);
    tft.setTextColor(c);
    tft.setTextSize(2);
    tft.setCursor(cx - 6, cy - 38);
    tft.print("?");
}

// ─── Mouth primitives ───────────────────────────────────────────────────

// ω-mouth: two small upward bumps (3-arc shape).
static void mouth_omega(int cx, int cy, int w = 48, uint16_t c = C_WHITE) {
    int hw = w / 2;
    int b  = hw / 2;
    for (int dx = -hw; dx <= hw; dx++) {
        float v = 0;
        // Two bumps
        float t1 = (float)(dx + b / 2) / (b / 1.2f);
        float t2 = (float)(dx - b / 2) / (b / 1.2f);
        if (t1 > -1 && t1 < 1) v = max(v, (1.0f - t1 * t1) * 6.5f);
        if (t2 > -1 && t2 < 1) v = max(v, (1.0f - t2 * t2) * 6.5f);
        for (int s = 0; s < 2; s++) tft.drawPixel(cx + dx, cy - (int)v - s, c);
    }
    // Outer corner uplifts
    tft.drawPixel(cx - hw,     cy - 1, c);
    tft.drawPixel(cx + hw,     cy - 1, c);
}

// Smile arc (upturned mouth).
static void mouth_smile(int cx, int cy, int w = 30, int h = 8, uint16_t c = C_WHITE) {
    for (int dx = -w / 2; dx <= w / 2; dx++) {
        float t = (float)dx / (w / 2.0f);
        int dy = (int)(h * (1.0f - t * t));
        for (int s = 0; s < 2; s++) tft.drawPixel(cx + dx, cy - dy + s, c);
    }
}

// Frown arc (downturned mouth).
static void mouth_frown(int cx, int cy, int w = 30, int h = 10, uint16_t c = C_WHITE) {
    for (int dx = -w / 2; dx <= w / 2; dx++) {
        float t = (float)dx / (w / 2.0f);
        int dy = (int)(h * (1.0f - t * t));
        for (int s = 0; s < 2; s++) tft.drawPixel(cx + dx, cy + dy - s, c);
    }
}

// Underscore _.
static void mouth_line(int cx, int cy, int w = 24, uint16_t c = C_WHITE) {
    tft.fillRect(cx - w / 2, cy, w, 3, c);
}

// 益-style gritted teeth — grid of small rectangles.
static void mouth_gritted(int cx, int cy, int w = 50, int h = 12, uint16_t c = C_WHITE) {
    int x0 = cx - w / 2;
    tft.drawLine(x0, cy,         x0 + w, cy,         c);
    tft.drawLine(x0, cy + h,     x0 + w, cy + h,     c);
    tft.drawLine(x0, cy + 1,     x0 + w, cy + 1,     c);
    tft.drawLine(x0, cy + h - 1, x0 + w, cy + h - 1, c);
    // Vertical separators
    for (int i = 0; i <= 5; i++) {
        int x = x0 + i * (w / 5);
        tft.drawLine(x, cy, x, cy + h, c);
    }
}

// Tiny o-shape mouth.
static void mouth_o(int cx, int cy, int r = 6, uint16_t c = C_WHITE) {
    tft.drawCircle(cx, cy, r, c);
    tft.drawCircle(cx, cy, r - 1, c);
}

// ─── Decorations ────────────────────────────────────────────────────────

static void draw_tears(int cx, int cy_eye) {
    // teardrop hanging below the eye
    tft.fillCircle(cx, cy_eye + 28, 5, C_BLUE);
    tft.fillTriangle(cx - 5, cy_eye + 26, cx + 5, cy_eye + 26, cx, cy_eye + 12, C_BLUE);
    // a second smaller drop trailing
    tft.fillCircle(cx + 8, cy_eye + 42, 3, C_BLUE);
}

static void draw_blush(int cx, int cy, uint16_t c = C_PINK) {
    tft.fillCircle(cx,     cy, 5, c);
    tft.fillCircle(cx + 8, cy, 4, c);
}

static void draw_sparkles() {
    // little dot-stars in the upper corners for the LOVE face
    int xs[] = { 25, 50, 35, SCR_W - 25, SCR_W - 50, SCR_W - 35 };
    int ys[] = { 20, 40, 60, 20, 40, 60 };
    for (int i = 0; i < 6; i++) {
        tft.fillCircle(xs[i], ys[i], 2, C_PINK);
    }
}

static void draw_zzz() {
    tft.setTextColor(C_WHITE);
    tft.setTextSize(2);
    tft.setCursor(SCR_W - 70, 30); tft.print("z");
    tft.setTextSize(3);
    tft.setCursor(SCR_W - 55, 20); tft.print("Z");
    tft.setTextSize(2);
    tft.setCursor(SCR_W - 30, 14); tft.print("z");
}

static void draw_arms_up() {
    // ノ ノ — two diagonal arms thrown up from the lower edge
    for (int t = 0; t < 3; t++) {
        tft.drawLine(30 + t, 160, 55 + t, 100, C_WHITE);
        tft.drawLine(SCR_W - 30 - t, 160, SCR_W - 55 - t, 100, C_WHITE);
    }
    // little hand "fists"
    tft.fillCircle(55, 98, 4, C_WHITE);
    tft.fillCircle(SCR_W - 55, 98, 4, C_WHITE);
}

static void draw_motion_lines() {
    // 彡 — diagonal speed strokes between face and table
    for (int i = 0; i < 4; i++) {
        int y0 = 35 + i * 12;
        tft.drawLine(SCR_W - 100, y0,     SCR_W - 80, y0 - 8, C_ORANGE);
        tft.drawLine(SCR_W - 100, y0 + 1, SCR_W - 80, y0 - 7, C_ORANGE);
    }
}

static void draw_flying_table() {
    // ┻━┻ flung at an angle in the upper-right corner
    int tx = SCR_W - 70, ty = 18;
    tft.fillRect(tx, ty, 50, 5, C_BROWN);
    tft.fillRect(tx + 10, ty + 5, 5, 16, C_BROWN);
    tft.fillRect(tx + 35, ty + 5, 5, 16, C_BROWN);
    // motion line off the right edge
    tft.drawLine(tx + 50, ty + 2,  tx + 60, ty - 6,  C_BROWN);
    tft.drawLine(tx + 50, ty + 3,  tx + 60, ty - 5,  C_BROWN);
}

// ─── Render ─────────────────────────────────────────────────────────────
static void render() {
    uint16_t bg = C_BLACK;
    switch (s_face) {
        case F_HAPPY:      bg = 0x0220; break;
        case F_SAD:        bg = 0x0008; break;
        case F_CRY:        bg = 0x0010; break;
        case F_ANGRY:      bg = 0x2000; break;
        case F_LOVE:       bg = 0x4924; break;   // dark plum
        case F_SLEEP:      bg = C_BLACK; break;
        case F_WALK:       bg = 0x0010; break;
        case F_TABLE_FLIP: bg = 0x2000; break;   // angry-red tinted
        default:           bg = C_BLACK;
    }
    tft.fillScreen(bg);

    if (s_blink_on) {
        eye_closed(EYE_L_X, EYE_Y);
        eye_closed(EYE_R_X, EYE_Y);
        return;
    }

    // Glance offsets (only the IDLE-ish faces with pupils respect these)
    int gx = 0, gy = 0;
    if (s_face == F_IDLE || s_face == F_SAD || s_face == F_CURIOUS) {
        gx = s_glance_x;
        gy = s_glance_y;
    }

    switch (s_face) {
        case F_HAPPY:
            // (^ω^)
            eye_caret(EYE_L_X, EYE_Y);
            eye_caret(EYE_R_X, EYE_Y);
            mouth_omega(SCR_W / 2, 130);
            draw_blush(50, 110);
            draw_blush(SCR_W - 65, 110);
            break;

        case F_SAD:
            // (︶︹︶)
            eye_arc_down(EYE_L_X, EYE_Y - 5);
            eye_arc_down(EYE_R_X, EYE_Y - 5);
            mouth_frown(SCR_W / 2, 125, 38, 12);
            break;

        case F_CRY:
            // (T_T)
            eye_t(EYE_L_X, EYE_Y);
            eye_t(EYE_R_X, EYE_Y);
            mouth_line(SCR_W / 2, 135, 22);
            draw_tears(EYE_L_X, EYE_Y);
            draw_tears(EYE_R_X, EYE_Y);
            break;

        case F_ANGRY:
            // (ಠ益ಠ)
            eye_glare(EYE_L_X, EYE_Y);
            eye_glare(EYE_R_X, EYE_Y);
            mouth_gritted(SCR_W / 2, 125, 50, 12);
            break;

        case F_LOVE:
            // (♡μ_μ)
            eye_heart(EYE_L_X, EYE_Y - 4);
            eye_heart(EYE_R_X, EYE_Y - 4);
            mouth_smile(SCR_W / 2, 130, 22, 6, C_PINK);
            draw_sparkles();
            break;

        case F_SLEEP:
            // (=_=) zZz
            eye_closed(EYE_L_X, EYE_Y, 30);
            eye_closed(EYE_R_X, EYE_Y, 30);
            mouth_line(SCR_W / 2, 130, 18, C_GRAY);
            draw_zzz();
            break;

        case F_SEARCH:
            // (•_•) panning pupils
            eye_dot(EYE_L_X, EYE_Y, s_search_off, 0);
            eye_dot(EYE_R_X, EYE_Y, s_search_off, 0);
            mouth_o(SCR_W / 2, 130, 5);
            break;

        case F_CURIOUS:
            // (?_?)
            eye_question(EYE_L_X, EYE_Y);
            eye_question(EYE_R_X, EYE_Y);
            mouth_o(SCR_W / 2, 130, 4);
            break;

        case F_WALK: {
            // 4-phase gait keyframes: contact → lift → swing → settle.
            static const int8_t kEyeY[4] = { -2, 2, 5, 1 };
            static const int8_t kMouthY[4] = { -1, 2, 4, 0 };
            static const int8_t kPupilX[4] = { 2, 1, -1, -2 };
            const uint8_t phase = s_walk_phase;
            eye_dot(EYE_L_X, EYE_Y + kEyeY[phase], kPupilX[phase], 0);
            eye_dot(EYE_R_X, EYE_Y + kEyeY[phase], kPupilX[phase], 0);
            mouth_smile(SCR_W / 2, 130 + kMouthY[phase], 25, 6);
            tft.fillRoundRect(SCR_W / 2 - 4, 132 + kMouthY[phase], 8, 6, 3, C_RED);  // tiny tongue
            break;
        }

        case F_TABLE_FLIP:
            // (ノಠ益ಠ)ノ彡┻━┻
            eye_glare(EYE_L_X + 10, EYE_Y + 10);
            eye_glare(EYE_R_X - 10, EYE_Y + 10);
            mouth_gritted(SCR_W / 2, 130, 50, 12);
            draw_arms_up();
            draw_motion_lines();
            draw_flying_table();
            break;

        case F_IDLE:
        default:
            // (·_·)
            eye_dot(EYE_L_X, EYE_Y, gx, gy);
            eye_dot(EYE_R_X, EYE_Y, gx, gy);
            mouth_line(SCR_W / 2, 130, 24);
            break;
    }
}

static const char* face_name(FaceMode f) {
    switch (f) {
        case F_IDLE:       return "IDLE";
        case F_HAPPY:      return "HAPPY";
        case F_SAD:        return "SAD";
        case F_CRY:        return "CRY";
        case F_ANGRY:      return "ANGRY";
        case F_LOVE:       return "LOVE";
        case F_SLEEP:      return "SLEEP";
        case F_SEARCH:     return "SEARCH";
        case F_CURIOUS:    return "CURIOUS";
        case F_WALK:       return "WALK";
        case F_TABLE_FLIP: return "TABLE_FLIP";
        default:           return "?";
    }
}

static bool parse_face(const String& s, FaceMode& out) {
    if      (s == "IDLE")       out = F_IDLE;
    else if (s == "HAPPY")      out = F_HAPPY;
    else if (s == "SAD")        out = F_SAD;
    else if (s == "CRY")        out = F_CRY;
    else if (s == "ANGRY")      out = F_ANGRY;
    else if (s == "LOVE")       out = F_LOVE;
    else if (s == "SLEEP")      out = F_SLEEP;
    else if (s == "SEARCH")     out = F_SEARCH;
    else if (s == "CURIOUS")    out = F_CURIOUS;
    else if (s == "WALK")       out = F_WALK;
    else if (s == "TABLE_FLIP" || s == "FLIP") out = F_TABLE_FLIP;
    else return false;
    return true;
}

static void handle_line(String& line) {
    line.trim();
    if (!line.length()) return;
    if (line.equalsIgnoreCase("PING")) { Serial.println("pong"); return; }
    line.toUpperCase();
    if (line.startsWith("FACE:")) {
        String f = line.substring(5);
        if (f == "BLINK") {
            s_blink_on    = true;
            s_blink_until = millis() + 150;
            render();
            Serial.println("ok blink");
            return;
        }
        FaceMode m;
        if (!parse_face(f, m)) { Serial.print("? bad face: "); Serial.println(f); return; }
        s_face = m;
        s_blink_on = false;
        s_glance_x = s_glance_y = 0;
        s_next_blink   = millis() + 2500 + random(0, 2000);
        s_next_glance  = millis() + 3500 + random(0, 2500);
        s_next_search  = millis() + 110;
        s_next_walk    = millis() + 200;
        render();
        Serial.print("ok face="); Serial.println(face_name(s_face));
        return;
    }
    Serial.print("? unknown: "); Serial.println(line);
}

static void pump_serial(Stream& port) {
    while (port.available()) {
        char c = (char)port.read();
        if (c == '\n' || c == '\r') {
            if (s_buf.length()) {
                handle_line(s_buf);
                s_buf = "";
            }
        } else if (s_buf.length() < 64) {
            s_buf += c;
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=== Marvin C6 face booting ===");

    // Wired main-board → C6 screen control.
    Serial1.begin(921600, SERIAL_8N1, /*rx*/16, /*tx*/17);

    // Backlight PWM (v3.x API).
    ledcAttach(TFT_BL, 5000, 8);
    ledcWrite(TFT_BL, 80);                  // ~30% — runs cooler than full-on

    SPI.begin(TFT_SCLK, /*MISO*/ -1, TFT_MOSI, TFT_CS);
    tft.init(SCR_H, SCR_W);
    tft.setRotation(1);
    tft.fillScreen(C_BLACK);
    tft.setTextColor(C_WHITE);

    render();

    uint32_t now = millis();
    s_next_blink  = now + 3000;
    s_next_glance = now + 4000;
    s_next_search = now + 500;
    s_next_walk   = now + 120;

    Serial.println("ready — try: FACE:HAPPY  FACE:WALK  FACE:TABLE_FLIP  FACE:LOVE");
}

void loop() {
    pump_serial(Serial);
    pump_serial(Serial1);

    uint32_t now = millis();

    // End-of-blink
    if (s_blink_on && now >= s_blink_until) {
        s_blink_on = false;
        s_next_blink = now + 2500 + random(0, 2000);
        render();
    }

    bool glance_face =
        (s_face == F_IDLE || s_face == F_HAPPY || s_face == F_CURIOUS || s_face == F_SAD);

    // Blink scheduler
    if (glance_face && !s_blink_on && now >= s_next_blink) {
        s_blink_on = true;
        s_blink_until = now + 130;
        render();
    }

    // Idle glance
    if (glance_face && !s_blink_on) {
        if (s_glance_x == 0 && s_glance_y == 0 && now >= s_next_glance) {
            static const int8_t DIRS[][2] = {
                {-12,  0}, {12,  0}, {0, -6}, {-10, -5}, {10, -5}, {-9, 5}, {9, 5},
            };
            const int N = sizeof(DIRS) / sizeof(DIRS[0]);
            int i = random(0, N);
            s_glance_x = DIRS[i][0];
            s_glance_y = DIRS[i][1];
            s_glance_clear = now + 500 + random(0, 600);
            s_next_glance  = now + 3500 + random(0, 3500);
            render();
        } else if ((s_glance_x || s_glance_y) && now >= s_glance_clear) {
            s_glance_x = 0;
            s_glance_y = 0;
            render();
        }
    }

    // Search pan
    if (s_face == F_SEARCH && !s_blink_on && now >= s_next_search) {
        s_search_off += s_search_dir * 6;
        if (s_search_off >  18) s_search_dir = -1;
        if (s_search_off < -18) s_search_dir = +1;
        s_next_search = now + 110;
        render();
    }

    // Walk bounce
    if (s_face == F_WALK && !s_blink_on && now >= s_next_walk) {
        s_walk_phase = (uint8_t)((s_walk_phase + 1) % WALK_PHASE_COUNT);
        s_next_walk = now + 120;
        render();
    }

    delay(8);
}
