/*
 *  PetBot — C6 head-display sketch
 *  ───────────────────────────────
 *  Board     : Waveshare ESP32-C6-LCD-1.47 (onboard ST7789 172×320)
 *  Role      : Animated face for the PetBot. Listens on Serial for
 *              FACE:NAME commands and renders the matching expression.
 *
 *  ── Commands (line-terminated; LF or CR both OK) ───────────────────────
 *    FACE:IDLE      neutral, gentle blink
 *    FACE:HAPPY     squinty eyes, big smile
 *    FACE:SAD       drooping eyes, frown
 *    FACE:ANGRY     angled eyebrows, straight mouth
 *    FACE:SLEEP     closed eyes, "zZz"
 *    FACE:SEARCH    pupils panning left/right
 *    FACE:CURIOUS   one raised eyebrow
 *    FACE:BLINK     one-shot blink, then return to current face
 *    PING           replies "pong" — link healthcheck
 *
 *  ── How to test it now (no PetBot wiring required) ─────────────────────
 *    1. Flash this sketch to the C6 via its USB-C port
 *    2. Open Tools → Serial Monitor at 115200
 *    3. Type   FACE:HAPPY   + Enter   → screen changes
 *    4. Same monitor will echo `ok face=HAPPY`
 *
 *  ── How to wire bot → C6 later (proper link) ───────────────────────────
 *    Body ESP32-CAM   →   C6 GPIO 16 (Serial1 RX)
 *    Body GND         →   C6 GND
 *    (no return wire needed for now — one-way commands)
 *    Then uncomment the Serial1 lines in setup() / loop() below.
 *
 *  ── Arduino IDE setup ──────────────────────────────────────────────────
 *    Tools → Board → ESP32 Arduino → "ESP32C6 Dev Module"
 *    Tools → USB CDC On Boot      → Enabled
 *    Tools → Flash Mode           → QIO
 *    Tools → Partition Scheme     → Default 4MB
 *    Tools → Upload Speed         → 921600
 *
 *  ── Libraries (install via Tools → Manage Libraries) ───────────────────
 *    - Adafruit GFX Library                  by Adafruit
 *    - Adafruit ST7735 and ST7789 Library    by Adafruit
 */

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// ─── ST7789 wiring on the C6-LCD-1.47 (do NOT change — board-fixed) ─────
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
    F_IDLE = 0, F_HAPPY, F_SAD, F_ANGRY, F_SLEEP, F_SEARCH, F_CURIOUS,
};

static FaceMode  s_face        = F_IDLE;
static bool      s_blink_on    = false;
static uint32_t  s_blink_until = 0;
static uint32_t  s_next_blink  = 0;
static int8_t    s_search_dir  = 1;
static uint32_t  s_next_search = 0;
static int8_t    s_search_off  = 0;
static String    s_buf;

// ─── Drawing helpers ────────────────────────────────────────────────────
static void fill_bg(uint16_t c) { tft.fillScreen(c); }

// Eyes: two rounded rectangles, optionally with pupils.
// eye_h = vertical height (small → closed/blink, big → wide).
static void draw_eyes(int eye_y, int eye_h, int eye_w = 60, int pupil_off = 0,
                      uint16_t eye_c = ST77XX_WHITE, uint16_t pupil_c = ST77XX_BLACK,
                      bool draw_pupils = true) {
    const int lx = 100;
    const int rx = SCR_W - 100;
    tft.fillRoundRect(lx - eye_w / 2, eye_y, eye_w, eye_h, 8, eye_c);
    tft.fillRoundRect(rx - eye_w / 2, eye_y, eye_w, eye_h, 8, eye_c);
    if (draw_pupils && eye_h > 14) {
        const int cy = eye_y + eye_h / 2;
        tft.fillCircle(lx + pupil_off, cy, 8, pupil_c);
        tft.fillCircle(rx + pupil_off, cy, 8, pupil_c);
    }
}

// Smile = bottom half of a circle.
static void draw_smile(int cx, int cy, int r, uint16_t c) {
    tft.drawCircle(cx, cy, r, c);
    tft.drawCircle(cx, cy, r - 1, c);
    tft.fillRect(cx - r - 1, cy - r - 1, 2 * r + 2, r + 1, ST77XX_BLACK);
}

// Frown = top half of a circle.
static void draw_frown(int cx, int cy, int r, uint16_t c) {
    tft.drawCircle(cx, cy, r, c);
    tft.drawCircle(cx, cy, r - 1, c);
    tft.fillRect(cx - r - 1, cy, 2 * r + 2, r + 2, ST77XX_BLACK);
}

static void render() {
    // background tinted per mood
    uint16_t bg = ST77XX_BLACK;
    if      (s_face == F_HAPPY)   bg = 0x0220;   // dark green tint
    else if (s_face == F_SAD)     bg = 0x0008;   // very dark blue
    else if (s_face == F_ANGRY)   bg = 0x2000;   // dark red
    else if (s_face == F_SLEEP)   bg = 0x0000;
    fill_bg(bg);

    if (s_blink_on) {
        // Closed-eye slits across every face mode
        draw_eyes(80, 4, 60, 0, ST77XX_WHITE, ST77XX_BLACK, false);
        return;
    }

    switch (s_face) {
        case F_HAPPY:
            // Big upward-arc eyes (smile-y), bright mouth
            draw_eyes(50, 30, 60, 0, ST77XX_WHITE, ST77XX_BLACK, false);
            // pupils that look up
            tft.fillCircle(100, 60, 6, ST77XX_BLACK);
            tft.fillCircle(SCR_W - 100, 60, 6, ST77XX_BLACK);
            // big smile
            draw_smile(SCR_W / 2, 110, 30, ST77XX_WHITE);
            // rosy cheeks
            tft.fillCircle(60, 110, 8, 0xF800);
            tft.fillCircle(SCR_W - 60, 110, 8, 0xF800);
            break;

        case F_SAD:
            draw_eyes(60, 40, 60, 0, 0x6F1F, ST77XX_BLACK, true);
            draw_frown(SCR_W / 2, 150, 25, ST77XX_WHITE);
            // tear drop on the left
            tft.fillCircle(80, 105, 4, 0x041F);
            break;

        case F_ANGRY: {
            // Hard, narrowed eyes
            draw_eyes(70, 22, 60, 0, ST77XX_WHITE, ST77XX_BLACK, true);
            // angled eyebrows ( \  / )
            tft.drawLine( 70, 50, 130, 65, ST77XX_RED);
            tft.drawLine( 70, 51, 130, 66, ST77XX_RED);
            tft.drawLine(SCR_W - 70, 50, SCR_W - 130, 65, ST77XX_RED);
            tft.drawLine(SCR_W - 70, 51, SCR_W - 130, 66, ST77XX_RED);
            // straight mouth, slightly downturned at the ends
            tft.drawLine(SCR_W / 2 - 25, 135, SCR_W / 2 + 25, 135, ST77XX_WHITE);
            tft.drawLine(SCR_W / 2 - 25, 135, SCR_W / 2 - 32, 142, ST77XX_WHITE);
            tft.drawLine(SCR_W / 2 + 25, 135, SCR_W / 2 + 32, 142, ST77XX_WHITE);
            break;
        }

        case F_SLEEP:
            // Closed-eye arcs ( ⌣ )
            for (int i = 0; i < 2; i++) {
                int cx = (i == 0) ? 100 : SCR_W - 100;
                tft.drawLine(cx - 22, 90, cx - 10, 96, ST77XX_WHITE);
                tft.drawLine(cx - 10, 96, cx + 10, 96, ST77XX_WHITE);
                tft.drawLine(cx + 10, 96, cx + 22, 90, ST77XX_WHITE);
            }
            // zZz
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

        case F_SEARCH: {
            // Wide eyes, pupils offset by s_search_off
            draw_eyes(55, 50, 60, s_search_off, ST77XX_WHITE, ST77XX_BLACK, true);
            // small "o" mouth
            tft.drawCircle(SCR_W / 2, 130, 7, ST77XX_WHITE);
            break;
        }

        case F_CURIOUS:
            draw_eyes(60, 40, 60, 0, ST77XX_WHITE, ST77XX_BLACK, true);
            // raised right eyebrow
            tft.drawLine(SCR_W - 130, 50, SCR_W - 70, 38, ST77XX_WHITE);
            tft.drawLine(SCR_W - 130, 51, SCR_W - 70, 39, ST77XX_WHITE);
            // small open mouth
            tft.drawCircle(SCR_W / 2, 130, 6, ST77XX_WHITE);
            break;

        case F_IDLE:
        default:
            draw_eyes(60, 40, 60, 0, ST77XX_WHITE, ST77XX_BLACK, true);
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
        s_next_blink = millis() + 2500 + random(0, 2000);
        s_next_search = millis() + 500;
        render();
        Serial.print("ok face="); Serial.println(face_name(s_face));
        return;
    }
    Serial.print("? unknown: "); Serial.println(line);
}

// ─── Drain a Serial source one byte at a time, calling handle_line(buf)
// on each complete line.
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
    // (Arduino-ESP32 v3.x API — v2.x would use ledcSetup + ledcAttachPin.)
    ledcAttach(TFT_BL, 5000 /*Hz*/, 8 /*-bit*/);
    ledcWrite(TFT_BL, 80);   // 0–255, ~30%

    SPI.begin(TFT_SCLK, /*MISO*/ -1, TFT_MOSI, TFT_CS);
    tft.init(SCR_H, SCR_W);     // native portrait dims
    tft.setRotation(1);          // landscape 320×172
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE);

    render();

    s_next_blink  = millis() + 3000;
    s_next_search = millis() + 500;

    Serial.println("ready — try: FACE:HAPPY  /  FACE:IDLE  /  FACE:BLINK  /  PING");
}

void loop() {
    pump_serial(Serial);
    // pump_serial(Serial1);   // uncomment after wiring body TX → C6 RX

    uint32_t now = millis();

    // End-of-blink return
    if (s_blink_on && now >= s_blink_until) {
        s_blink_on = false;
        s_next_blink = now + 2500 + random(0, 2000);
        render();
    }

    // Idle blink loop on the awake faces
    bool blinking_face =
        (s_face == F_IDLE || s_face == F_HAPPY || s_face == F_CURIOUS || s_face == F_SAD);
    if (blinking_face && !s_blink_on && now >= s_next_blink) {
        s_blink_on = true;
        s_blink_until = now + 130;
        render();
    }

    // Search-eye pan
    if (s_face == F_SEARCH && !s_blink_on && now >= s_next_search) {
        s_search_off += s_search_dir * 6;
        if (s_search_off >  18) s_search_dir = -1;
        if (s_search_off < -18) s_search_dir = +1;
        s_next_search = now + 110;
        render();
    }

    delay(8);
}
