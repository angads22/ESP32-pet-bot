/*
 *  PetBot — C6 head-display sketch
 *  ───────────────────────────────
 *  Board     : Waveshare ESP32-C6-LCD-1.47 (onboard ST7789 172×320)
 *  Role      : Animated face for the PetBot. Listens on Serial for
 *              FACE:NAME commands and renders the matching expression.
 *
 *  ── Commands (line-terminated; LF or CR both OK) ───────────────────────
 *    FACE:IDLE      neutral, gentle blink + occasional glances
 *    FACE:HAPPY     squinty eyes, big smile, glances
 *    FACE:SAD       drooping eyes, frown, glances
 *    FACE:ANGRY     angled eyebrows, narrowed eyes
 *    FACE:SLEEP     closed eyes, "zZz"
 *    FACE:SEARCH    pupils panning left/right scan
 *    FACE:CURIOUS   one raised eyebrow, glances
 *    FACE:WALK      forward-looking eyes, gentle vertical bounce
 *    FACE:BLINK     one-shot blink, returns to current face
 *    PING           replies "pong" — link healthcheck
 *
 *  ── Animation hooks ───────────────────────────────────────────────────
 *    - IDLE / HAPPY / SAD / CURIOUS: random glances every 3–6 s + blink
 *    - WALK:                         vertical bounce at ~5 Hz
 *    - SEARCH:                       pupils pan ±18 px ~9 Hz
 *    - SLEEP:                        zZz "stack" stays put (frame-bound)
 *
 *  ── How to test now (no bot wiring required) ──────────────────────────
 *    Flash via the C6's USB-C, open Serial Monitor at 115200,
 *    type   FACE:HAPPY   + Enter, screen changes.
 *    Type   FACE:WALK    to see the bounce.
 *
 *  ── How to wire bot → C6 later (proper link) ──────────────────────────
 *    Body GPIO 4 (TX)   →   C6 GPIO 16 (Serial1 RX)
 *    Body GND           →   C6 GND
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

#define SCR_W    320   // after setRotation(1)
#define SCR_H    172

static Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, TFT_CS, TFT_DC, TFT_RST);

// ─── Face state ─────────────────────────────────────────────────────────
enum FaceMode : uint8_t {
    F_IDLE = 0, F_HAPPY, F_SAD, F_ANGRY, F_SLEEP, F_SEARCH, F_CURIOUS, F_WALK,
};

static FaceMode  s_face        = F_IDLE;
static bool      s_blink_on    = false;
static uint32_t  s_blink_until = 0;
static uint32_t  s_next_blink  = 0;

// Idle-glance animation: pupils drift to a random direction every few
// seconds, hold briefly, then return to center. Looks like "looking
// around" while idle/happy/curious/sad.
static int8_t    s_glance_x      = 0;
static int8_t    s_glance_y      = 0;
static uint32_t  s_next_glance   = 0;
static uint32_t  s_glance_clear  = 0;

// Search pupils pan
static int8_t    s_search_dir  = 1;
static uint32_t  s_next_search = 0;
static int8_t    s_search_off  = 0;

// Walk bounce
static int8_t    s_walk_phase  = 0;
static uint32_t  s_next_walk   = 0;

static String    s_buf;

// ─── Drawing helpers ────────────────────────────────────────────────────
static void fill_bg(uint16_t c) { tft.fillScreen(c); }

// Eyes: two rounded rectangles, optionally with pupils.
// eye_h = vertical height (small → closed/blink, big → wide).
// pupil_x_off / pupil_y_off shift both pupils inside the eye (used for
// idle glances and search panning).
static void draw_eyes(int eye_y, int eye_h, int eye_w = 60,
                      int pupil_x_off = 0, int pupil_y_off = 0,
                      uint16_t eye_c = ST77XX_WHITE, uint16_t pupil_c = ST77XX_BLACK,
                      bool draw_pupils = true) {
    const int lx = 100;
    const int rx = SCR_W - 100;
    tft.fillRoundRect(lx - eye_w / 2, eye_y, eye_w, eye_h, 8, eye_c);
    tft.fillRoundRect(rx - eye_w / 2, eye_y, eye_w, eye_h, 8, eye_c);
    if (draw_pupils && eye_h > 14) {
        const int cy = eye_y + eye_h / 2;
        tft.fillCircle(lx + pupil_x_off, cy + pupil_y_off, 8, pupil_c);
        tft.fillCircle(rx + pupil_x_off, cy + pupil_y_off, 8, pupil_c);
    }
}

static void draw_smile(int cx, int cy, int r, uint16_t c) {
    tft.drawCircle(cx, cy, r, c);
    tft.drawCircle(cx, cy, r - 1, c);
    tft.fillRect(cx - r - 1, cy - r - 1, 2 * r + 2, r + 1, ST77XX_BLACK);
}

static void draw_frown(int cx, int cy, int r, uint16_t c) {
    tft.drawCircle(cx, cy, r, c);
    tft.drawCircle(cx, cy, r - 1, c);
    tft.fillRect(cx - r - 1, cy, 2 * r + 2, r + 2, ST77XX_BLACK);
}

static void render() {
    uint16_t bg = ST77XX_BLACK;
    if      (s_face == F_HAPPY)   bg = 0x0220;
    else if (s_face == F_SAD)     bg = 0x0008;
    else if (s_face == F_ANGRY)   bg = 0x2000;
    else if (s_face == F_WALK)    bg = 0x0010;   // dark blue tint
    fill_bg(bg);

    if (s_blink_on) {
        draw_eyes(80, 4, 60, 0, 0, ST77XX_WHITE, ST77XX_BLACK, false);
        return;
    }

    switch (s_face) {
        case F_HAPPY:
            // Squinty arc-eyes — small, looking up
            draw_eyes(50, 30, 60, 0, 0, ST77XX_WHITE, ST77XX_BLACK, false);
            // Tiny pupils that respect the glance state
            tft.fillCircle(100 + s_glance_x,           60 + s_glance_y, 6, ST77XX_BLACK);
            tft.fillCircle(SCR_W - 100 + s_glance_x,   60 + s_glance_y, 6, ST77XX_BLACK);
            draw_smile(SCR_W / 2, 110, 30, ST77XX_WHITE);
            tft.fillCircle(60, 110, 8, 0xF800);
            tft.fillCircle(SCR_W - 60, 110, 8, 0xF800);
            break;

        case F_SAD:
            draw_eyes(60, 40, 60, s_glance_x, s_glance_y, 0x6F1F, ST77XX_BLACK, true);
            draw_frown(SCR_W / 2, 150, 25, ST77XX_WHITE);
            tft.fillCircle(80, 105, 4, 0x041F);
            break;

        case F_ANGRY: {
            draw_eyes(70, 22, 60, 0, 0, ST77XX_WHITE, ST77XX_BLACK, true);
            tft.drawLine( 70, 50, 130, 65, ST77XX_RED);
            tft.drawLine( 70, 51, 130, 66, ST77XX_RED);
            tft.drawLine(SCR_W - 70, 50, SCR_W - 130, 65, ST77XX_RED);
            tft.drawLine(SCR_W - 70, 51, SCR_W - 130, 66, ST77XX_RED);
            tft.drawLine(SCR_W / 2 - 25, 135, SCR_W / 2 + 25, 135, ST77XX_WHITE);
            tft.drawLine(SCR_W / 2 - 25, 135, SCR_W / 2 - 32, 142, ST77XX_WHITE);
            tft.drawLine(SCR_W / 2 + 25, 135, SCR_W / 2 + 32, 142, ST77XX_WHITE);
            break;
        }

        case F_SLEEP:
            for (int i = 0; i < 2; i++) {
                int cx = (i == 0) ? 100 : SCR_W - 100;
                tft.drawLine(cx - 22, 90, cx - 10, 96, ST77XX_WHITE);
                tft.drawLine(cx - 10, 96, cx + 10, 96, ST77XX_WHITE);
                tft.drawLine(cx + 10, 96, cx + 22, 90, ST77XX_WHITE);
            }
            tft.setTextColor(ST77XX_WHITE);
            tft.setTextSize(2);
            tft.setCursor(SCR_W - 70, 30);
            tft.print("z");
            tft.setTextSize(3);
            tft.setCursor(SCR_W - 55, 20);
            tft.print("Z");
            tft.setTextSize(2);
            tft.setCursor(SCR_W - 30, 14);
            tft.print("z");
            break;

        case F_SEARCH:
            draw_eyes(55, 50, 60, s_search_off, 0, ST77XX_WHITE, ST77XX_BLACK, true);
            tft.drawCircle(SCR_W / 2, 130, 7, ST77XX_WHITE);
            break;

        case F_CURIOUS:
            draw_eyes(60, 40, 60, s_glance_x, s_glance_y, ST77XX_WHITE, ST77XX_BLACK, true);
            tft.drawLine(SCR_W - 130, 50, SCR_W - 70, 38, ST77XX_WHITE);
            tft.drawLine(SCR_W - 130, 51, SCR_W - 70, 39, ST77XX_WHITE);
            tft.drawCircle(SCR_W / 2, 130, 6, ST77XX_WHITE);
            break;

        case F_WALK: {
            // Gentle vertical bounce + forward-looking pupils + small open mouth
            int bounce = s_walk_phase ? 4 : -4;
            draw_eyes(60 + bounce, 40, 60, 0, 0, ST77XX_WHITE, ST77XX_BLACK, false);
            tft.fillCircle(100,           80 + bounce, 8, ST77XX_BLACK);
            tft.fillCircle(SCR_W - 100,   80 + bounce, 8, ST77XX_BLACK);
            // open mouth (slight smile, like a happy walk)
            tft.fillRoundRect(SCR_W / 2 - 12, 128 + bounce, 24, 9, 4, ST77XX_WHITE);
            // tiny tongue
            tft.fillRoundRect(SCR_W / 2 - 4, 132 + bounce, 8, 6, 3, 0xF800);
            break;
        }

        case F_IDLE:
        default:
            draw_eyes(60, 40, 60, s_glance_x, s_glance_y, ST77XX_WHITE, ST77XX_BLACK, true);
            tft.drawLine(SCR_W / 2 - 18, 130, SCR_W / 2 + 18, 130, ST77XX_WHITE);
            break;
    }
}

static const char* face_name(FaceMode f) {
    switch (f) {
        case F_IDLE:    return "IDLE";
        case F_HAPPY:   return "HAPPY";
        case F_SAD:     return "SAD";
        case F_ANGRY:   return "ANGRY";
        case F_SLEEP:   return "SLEEP";
        case F_SEARCH:  return "SEARCH";
        case F_CURIOUS: return "CURIOUS";
        case F_WALK:    return "WALK";
        default:        return "?";
    }
}

static bool parse_face(const String& s, FaceMode& out) {
    if      (s == "IDLE")    out = F_IDLE;
    else if (s == "HAPPY")   out = F_HAPPY;
    else if (s == "SAD")     out = F_SAD;
    else if (s == "ANGRY")   out = F_ANGRY;
    else if (s == "SLEEP")   out = F_SLEEP;
    else if (s == "SEARCH")  out = F_SEARCH;
    else if (s == "CURIOUS") out = F_CURIOUS;
    else if (s == "WALK")    out = F_WALK;
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
    Serial.begin(115200);   // USB CDC (when "USB CDC On Boot" = Enabled)
    delay(200);
    Serial.println("\n=== PetBot C6 face booting ===");

    // To later receive commands from the body MCU over UART (3 wires),
    // wire Body TX → C6 GPIO 16 (Serial1 RX) and Body GND → C6 GND,
    // then uncomment the line below:
    // Serial1.begin(115200, SERIAL_8N1, /*rx*/16, /*tx*/17);

    // Backlight on a PWM channel at ~30% brightness. Drops board-warmth
    // a lot vs. driving the BL pin fully high. If you need it brighter,
    // raise the second arg of ledcWrite() up to 255.
    ledcAttach(TFT_BL, 5000 /*Hz*/, 8 /*-bit*/);
    ledcWrite(TFT_BL, 80);   // 0–255, ~30%

    SPI.begin(TFT_SCLK, /*MISO*/ -1, TFT_MOSI, TFT_CS);
    tft.init(SCR_H, SCR_W);
    tft.setRotation(1);
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE);

    render();

    uint32_t now = millis();
    s_next_blink  = now + 3000;
    s_next_glance = now + 4000;
    s_next_search = now + 500;
    s_next_walk   = now + 200;

    Serial.println("ready — try: FACE:WALK  /  FACE:IDLE  /  FACE:HAPPY  /  FACE:BLINK");
}

void loop() {
    pump_serial(Serial);
    // pump_serial(Serial1);   // uncomment after wiring body TX → C6 RX

    uint32_t now = millis();

    // ─── Blink (end-of-blink) ────────────────────────────────────────
    if (s_blink_on && now >= s_blink_until) {
        s_blink_on = false;
        s_next_blink = now + 2500 + random(0, 2000);
        render();
    }

    bool glance_face =
        (s_face == F_IDLE || s_face == F_HAPPY || s_face == F_CURIOUS || s_face == F_SAD);

    // ─── Blink scheduler ─────────────────────────────────────────────
    if (glance_face && !s_blink_on && now >= s_next_blink) {
        s_blink_on = true;
        s_blink_until = now + 130;
        render();
    }

    // ─── Idle-glance scheduler ───────────────────────────────────────
    if (glance_face && !s_blink_on) {
        // Time to start a new glance?
        if (s_glance_x == 0 && s_glance_y == 0 && now >= s_next_glance) {
            // Pick a random direction. Keeps within a sensible eye box.
            static const int8_t DIRS[][2] = {
                {-14,  0}, {14,  0}, {0, -7}, {-12, -5}, {12, -5}, {-10, 5}, {10, 5}
            };
            const int N = sizeof(DIRS) / sizeof(DIRS[0]);
            int i = random(0, N);
            s_glance_x = DIRS[i][0];
            s_glance_y = DIRS[i][1];
            s_glance_clear = now + 500 + random(0, 600);
            s_next_glance  = now + 3500 + random(0, 3500);
            render();
        }
        // Time to return the pupils to center?
        else if ((s_glance_x || s_glance_y) && now >= s_glance_clear) {
            s_glance_x = 0;
            s_glance_y = 0;
            render();
        }
    }

    // ─── Search-eye pan ──────────────────────────────────────────────
    if (s_face == F_SEARCH && !s_blink_on && now >= s_next_search) {
        s_search_off += s_search_dir * 6;
        if (s_search_off >  18) s_search_dir = -1;
        if (s_search_off < -18) s_search_dir = +1;
        s_next_search = now + 110;
        render();
    }

    // ─── Walk bounce ─────────────────────────────────────────────────
    if (s_face == F_WALK && !s_blink_on && now >= s_next_walk) {
        s_walk_phase ^= 1;
        s_next_walk = now + 200;
        render();
    }

    delay(8);
}
