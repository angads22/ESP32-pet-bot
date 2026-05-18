/*
 *  PetBot / Marvin — C6 head-display sketch (expanded face set)
 *  ─────────────────────────────────────────────────────────────
 *  Board     : Waveshare ESP32-C6-LCD-1.47 (onboard ST7789 172×320)
 *
 *  Faces (drawn as graphic primitives — the vibe of the kaomoji):
 *
 *    Idle / rest
 *      FACE:IDLE         (·_·)        neutral; glances + blink + occasional yawn
 *      FACE:SLEEP        (=_=) zZz    closed eyes, drifting Zs
 *      FACE:COOL         (⌐■_■)       sunglasses + smug smirk
 *      FACE:WINK         (^_~)        one eye closed, slight smile
 *
 *    Positive
 *      FACE:HAPPY        (^ω^)        caret eyes, omega mouth, blush
 *      FACE:EXCITED      (★ω★)        star eyes, sparkles
 *      FACE:LOVE         (♡μ_μ)       heart eyes, pink lips
 *      FACE:CURIOUS      (?_?)        ringed eyes, floating "?"
 *
 *    Negative
 *      FACE:SAD          (︶︹︶)     arc-down eyes, frown
 *      FACE:CRY          (T_T)        T-eyes, tears
 *      FACE:ANGRY        (ಠ益ಠ)      glare + red brow + gritted teeth
 *      FACE:EMBARRASSED  (//ω//)      blush slashes, looking away
 *      FACE:DIZZY        (@_@)        spiral eyes, wavy mouth
 *      FACE:TABLE_FLIP   (ノಠ益ಠ)ノ彡┻━┻
 *
 *    Active / motion
 *      FACE:WALK                      gentle bounce + grin
 *      FACE:RUN                       fast bounce + tongue out + sweat
 *      FACE:SEARCH       (•_•)        pupils panning
 *      FACE:SURPRISED    (⊙_⊙)        huge eyes + tiny gape
 *
 *    One-shot
 *      FACE:BLINK                     150 ms blink, returns to current face
 *
 *    Misc
 *      PING                           replies "pong"
 *
 *  Auto-animations:
 *    - IDLE: glances + blink + a yawn every 15–25 s (eyes squint, mouth opens)
 *    - HAPPY / SAD / CURIOUS / SAD: glances + blink
 *    - SEARCH: pupils pan ±18 px (~9 Hz)
 *    - WALK:  ±4 px bounce  @ 5 Hz
 *    - RUN:   ±8 px bounce  @ 10 Hz
 *    - SLEEP / COOL / TABLE_FLIP / DIZZY: static
 *
 *  ── Wiring bot → C6 later ─────────────────────────────────────────────
 *    Body GPIO 4 (TX)  →  C6 GPIO 16 (Serial1 RX)
 *    Body GND          →  C6 GND
 *    Uncomment Serial1.begin(...) + pump_serial(Serial1) below.
 *
 *  ── Arduino IDE setup ─────────────────────────────────────────────────
 *    Tools → Board → ESP32 Arduino → "ESP32C6 Dev Module"
 *    Tools → USB CDC On Boot      → Enabled
 *    Tools → Partition Scheme     → Default 4MB
 *
 *  ── Libraries ─────────────────────────────────────────────────────────
 *    - Adafruit GFX Library
 *    - Adafruit ST7735 and ST7789 Library
 */

#include <Arduino.h>
#include <SPI.h>
#include <math.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// ─── ST7789 wiring (board-fixed) ────────────────────────────────────────
#define TFT_MOSI   6
#define TFT_SCLK   7
#define TFT_CS    14
#define TFT_DC    15
#define TFT_RST   21
#define TFT_BL    22

#define SCR_W    320
#define SCR_H    172

static Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, TFT_CS, TFT_DC, TFT_RST);

// ─── Faces ──────────────────────────────────────────────────────────────
enum FaceMode : uint8_t {
    F_IDLE = 0, F_HAPPY, F_SAD, F_CRY, F_ANGRY, F_LOVE,
    F_SLEEP, F_SEARCH, F_CURIOUS, F_WALK, F_RUN, F_TABLE_FLIP,
    F_SURPRISED, F_EXCITED, F_COOL, F_EMBARRASSED, F_DIZZY, F_WINK,
};

// ─── State ──────────────────────────────────────────────────────────────
static FaceMode  s_face        = F_IDLE;
static bool      s_blink_on    = false;
static uint32_t  s_blink_until = 0;
static uint32_t  s_next_blink  = 0;

// Glance ("looking around")
static int8_t    s_glance_x    = 0;
static int8_t    s_glance_y    = 0;
static uint32_t  s_next_glance = 0;
static uint32_t  s_glance_clear = 0;

// Idle yawn (eyes squint, mouth opens, then closes)
static uint8_t   s_yawn_phase   = 0;   // 0=off, 1=opening, 2=hold, 3=closing
static uint32_t  s_yawn_until   = 0;
static uint32_t  s_next_yawn    = 0;

// Search pan
static int8_t    s_search_dir  = 1;
static uint32_t  s_next_search = 0;
static int8_t    s_search_off  = 0;

// Walk/run bounce
static int8_t    s_walk_phase  = 0;
static uint32_t  s_next_walk   = 0;

static String    s_buf;

// ─── Colors ─────────────────────────────────────────────────────────────
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
#define C_GREEN   0x07E0

// Eye geometry
#define EYE_L_X   100
#define EYE_R_X   (SCR_W - 100)
#define EYE_Y      80

// ─── Eye primitives ─────────────────────────────────────────────────────
static void eye_caret(int cx, int cy, int w = 30, int h = 14, uint16_t c = C_WHITE) {
    int x0 = cx - w / 2, x1 = cx + w / 2;
    int yb = cy + h / 2, yt = cy - h / 2;
    for (int t = 0; t < 3; t++) {
        tft.drawLine(x0, yb + t, cx, yt + t, c);
        tft.drawLine(cx, yt + t, x1, yb + t, c);
    }
}

static void eye_arc_down(int cx, int cy, int w = 32, int h = 10, uint16_t c = C_WHITE) {
    for (int dx = -w / 2; dx <= w / 2; dx++) {
        float t = (float)dx / (w / 2.0f);
        int dy = (int)(h * (1.0f - t * t));
        for (int s = 0; s < 2; s++) tft.drawPixel(cx + dx, cy + dy - s, c);
    }
}

static void eye_t(int cx, int cy, uint16_t c = C_WHITE) {
    tft.fillRect(cx - 15, cy - 12, 30, 4, c);
    tft.fillRect(cx - 2,  cy - 12, 4,  24, c);
}

static void eye_heart(int cx, int cy, uint16_t c = C_PINK) {
    tft.fillCircle(cx - 8, cy - 4, 10, c);
    tft.fillCircle(cx + 8, cy - 4, 10, c);
    tft.fillTriangle(cx - 17, cy + 1, cx + 17, cy + 1, cx, cy + 18, c);
}

static void eye_glare(int cx, int cy, uint16_t c = C_WHITE) {
    tft.fillCircle(cx, cy + 2, 14, c);
    tft.fillCircle(cx, cy + 2, 5, C_BLACK);
    for (int t = 0; t < 5; t++) {
        tft.drawLine(cx - 20, cy - 18 + t, cx + 16, cy - 24 + t, C_RED);
    }
}

static void eye_closed(int cx, int cy, int w = 30, uint16_t c = C_WHITE) {
    tft.fillRect(cx - w / 2, cy - 2, w, 4, c);
}

static void eye_dot(int cx, int cy, int pupil_x = 0, int pupil_y = 0,
                    uint16_t eye_c = C_WHITE, uint16_t pup_c = C_BLACK) {
    tft.fillCircle(cx, cy, 16, eye_c);
    tft.fillCircle(cx + pupil_x, cy + pupil_y, 6, pup_c);
}

static void eye_question(int cx, int cy, uint16_t c = C_WHITE) {
    tft.drawCircle(cx, cy, 14, c);
    tft.drawCircle(cx, cy, 13, c);
    tft.fillCircle(cx, cy, 4, c);
    tft.setTextColor(c);
    tft.setTextSize(2);
    tft.setCursor(cx - 6, cy - 38);
    tft.print("?");
}

// Huge wide eye for SURPRISED
static void eye_wide(int cx, int cy, uint16_t c = C_WHITE) {
    tft.fillCircle(cx, cy, 22, c);
    tft.drawCircle(cx, cy, 23, C_WHITE);
    tft.fillCircle(cx, cy, 6, C_BLACK);
}

// 4-point star eye for EXCITED
static void eye_star(int cx, int cy, int r = 16, uint16_t c = C_YELLOW) {
    tft.fillTriangle(cx, cy - r, cx - 4, cy, cx + 4, cy, c);            // top
    tft.fillTriangle(cx, cy + r, cx - 4, cy, cx + 4, cy, c);            // bottom
    tft.fillTriangle(cx - r, cy, cx, cy - 4, cx, cy + 4, c);            // left
    tft.fillTriangle(cx + r, cy, cx, cy - 4, cx, cy + 4, c);            // right
    tft.fillCircle(cx, cy, 3, c);
}

// Spiral eye for DIZZY — 3 concentric circles, off-center
static void eye_spiral(int cx, int cy, uint16_t c = C_WHITE) {
    tft.drawCircle(cx, cy, 16, c);
    tft.drawCircle(cx + 1, cy + 1, 11, c);
    tft.drawCircle(cx + 2, cy + 2, 6, c);
    tft.fillCircle(cx + 3, cy + 3, 2, c);
}

// Yawn — squinted slit eye, slightly arched
static void eye_yawn(int cx, int cy, int progress, uint16_t c = C_WHITE) {
    // progress 0–100; map to eye height
    int h = 4 + (progress * 6 / 100);
    tft.fillRoundRect(cx - 18, cy - h / 2, 36, h, 3, c);
}

// ─── Mouth primitives ───────────────────────────────────────────────────
static void mouth_omega(int cx, int cy, int w = 48, uint16_t c = C_WHITE) {
    int hw = w / 2;
    int b  = hw / 2;
    for (int dx = -hw; dx <= hw; dx++) {
        float v = 0;
        float t1 = (float)(dx + b / 2) / (b / 1.2f);
        float t2 = (float)(dx - b / 2) / (b / 1.2f);
        if (t1 > -1 && t1 < 1) v = max(v, (1.0f - t1 * t1) * 6.5f);
        if (t2 > -1 && t2 < 1) v = max(v, (1.0f - t2 * t2) * 6.5f);
        for (int s = 0; s < 2; s++) tft.drawPixel(cx + dx, cy - (int)v - s, c);
    }
    tft.drawPixel(cx - hw, cy - 1, c);
    tft.drawPixel(cx + hw, cy - 1, c);
}

static void mouth_smile(int cx, int cy, int w = 30, int h = 8, uint16_t c = C_WHITE) {
    for (int dx = -w / 2; dx <= w / 2; dx++) {
        float t = (float)dx / (w / 2.0f);
        int dy = (int)(h * (1.0f - t * t));
        for (int s = 0; s < 2; s++) tft.drawPixel(cx + dx, cy - dy + s, c);
    }
}

static void mouth_frown(int cx, int cy, int w = 30, int h = 10, uint16_t c = C_WHITE) {
    for (int dx = -w / 2; dx <= w / 2; dx++) {
        float t = (float)dx / (w / 2.0f);
        int dy = (int)(h * (1.0f - t * t));
        for (int s = 0; s < 2; s++) tft.drawPixel(cx + dx, cy + dy - s, c);
    }
}

static void mouth_line(int cx, int cy, int w = 24, uint16_t c = C_WHITE) {
    tft.fillRect(cx - w / 2, cy, w, 3, c);
}

static void mouth_gritted(int cx, int cy, int w = 50, int h = 12, uint16_t c = C_WHITE) {
    int x0 = cx - w / 2;
    tft.drawLine(x0, cy,         x0 + w, cy,         c);
    tft.drawLine(x0, cy + h,     x0 + w, cy + h,     c);
    tft.drawLine(x0, cy + 1,     x0 + w, cy + 1,     c);
    tft.drawLine(x0, cy + h - 1, x0 + w, cy + h - 1, c);
    for (int i = 0; i <= 5; i++) {
        int x = x0 + i * (w / 5);
        tft.drawLine(x, cy, x, cy + h, c);
    }
}

static void mouth_o(int cx, int cy, int r = 6, uint16_t c = C_WHITE) {
    tft.drawCircle(cx, cy, r, c);
    tft.drawCircle(cx, cy, r - 1, c);
}

// Wavy ~ mouth for DIZZY
static void mouth_wave(int cx, int cy, int w = 36, uint16_t c = C_WHITE) {
    int hw = w / 2;
    for (int dx = -hw; dx <= hw; dx++) {
        int dy = (int)(3.0f * sinf(dx * 0.35f));
        for (int s = 0; s < 2; s++) tft.drawPixel(cx + dx, cy + dy + s, c);
    }
}

// Big open mouth (yawning, pant)
static void mouth_open(int cx, int cy, int w, int h, uint16_t c = C_WHITE, uint16_t fill = 0x4000) {
    tft.drawRoundRect(cx - w / 2, cy - h / 2, w, h, 6, c);
    tft.drawRoundRect(cx - w / 2 + 1, cy - h / 2 + 1, w - 2, h - 2, 6, c);
    tft.fillRoundRect(cx - w / 2 + 2, cy - h / 2 + 2, w - 4, h - 4, 5, fill);
}

// Panting tongue
static void draw_tongue(int cx, int cy, int len = 14, uint16_t c = C_RED) {
    tft.fillRoundRect(cx - 5, cy, 10, len, 4, c);
    tft.drawLine(cx, cy + 2, cx, cy + len - 2, C_BLACK);  // crease
}

// Smug smirk
static void mouth_smirk(int cx, int cy, uint16_t c = C_WHITE) {
    for (int t = 0; t < 2; t++) {
        tft.drawLine(cx - 10, cy + t, cx + 16, cy - 7 + t, c);
    }
}

// ─── Decorations ────────────────────────────────────────────────────────
static void draw_tears(int cx, int cy_eye) {
    tft.fillCircle(cx, cy_eye + 28, 5, C_BLUE);
    tft.fillTriangle(cx - 5, cy_eye + 26, cx + 5, cy_eye + 26, cx, cy_eye + 12, C_BLUE);
    tft.fillCircle(cx + 8, cy_eye + 42, 3, C_BLUE);
}

static void draw_sweat(int cx, int cy) {
    tft.fillCircle(cx, cy + 4, 3, C_BLUE);
    tft.fillTriangle(cx - 3, cy + 2, cx + 3, cy + 2, cx, cy - 5, C_BLUE);
}

static void draw_blush(int cx, int cy, uint16_t c = C_PINK) {
    tft.fillCircle(cx,     cy, 5, c);
    tft.fillCircle(cx + 8, cy, 4, c);
}

// // // — embarrassment slashes on the cheek
static void draw_blush_slashes(int cx, int cy) {
    for (int s = 0; s < 4; s++) {
        int x = cx + s * 6;
        for (int t = 0; t < 2; t++) tft.drawLine(x, cy + 6, x + 6, cy - 6 - t, C_PINK);
    }
}

static void draw_sparkles() {
    int xs[] = { 25, 50, 35, SCR_W - 25, SCR_W - 50, SCR_W - 35,
                 80, SCR_W - 80, 160 };
    int ys[] = { 20, 40, 60, 20, 40, 60, 25, 25, 18 };
    uint16_t col[] = { C_YELLOW, C_PINK, C_TEAL, C_YELLOW, C_PINK, C_TEAL, C_YELLOW, C_PINK, C_YELLOW };
    for (int i = 0; i < 9; i++) {
        tft.fillCircle(xs[i], ys[i], 2, col[i]);
        // four-point star tips
        tft.drawPixel(xs[i],     ys[i] - 4, col[i]);
        tft.drawPixel(xs[i],     ys[i] + 4, col[i]);
        tft.drawPixel(xs[i] - 4, ys[i],     col[i]);
        tft.drawPixel(xs[i] + 4, ys[i],     col[i]);
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
    for (int t = 0; t < 3; t++) {
        tft.drawLine(30 + t, 160, 55 + t, 100, C_WHITE);
        tft.drawLine(SCR_W - 30 - t, 160, SCR_W - 55 - t, 100, C_WHITE);
    }
    tft.fillCircle(55, 98, 4, C_WHITE);
    tft.fillCircle(SCR_W - 55, 98, 4, C_WHITE);
}

static void draw_motion_lines() {
    for (int i = 0; i < 4; i++) {
        int y0 = 35 + i * 12;
        tft.drawLine(SCR_W - 100, y0,     SCR_W - 80, y0 - 8, C_ORANGE);
        tft.drawLine(SCR_W - 100, y0 + 1, SCR_W - 80, y0 - 7, C_ORANGE);
    }
}

static void draw_flying_table() {
    int tx = SCR_W - 70, ty = 18;
    tft.fillRect(tx, ty, 50, 5, C_BROWN);
    tft.fillRect(tx + 10, ty + 5, 5, 16, C_BROWN);
    tft.fillRect(tx + 35, ty + 5, 5, 16, C_BROWN);
    tft.drawLine(tx + 50, ty + 2,  tx + 60, ty - 6,  C_BROWN);
    tft.drawLine(tx + 50, ty + 3,  tx + 60, ty - 5,  C_BROWN);
}

// Sunglasses — covers both eye positions
static void draw_sunglasses() {
    tft.fillRoundRect(EYE_L_X - 32, EYE_Y - 16, 64, 32, 8, C_BLACK);
    tft.fillRoundRect(EYE_R_X - 32, EYE_Y - 16, 64, 32, 8, C_BLACK);
    tft.fillRect(EYE_L_X + 32, EYE_Y - 3, EYE_R_X - EYE_L_X - 64, 6, C_BLACK);
    // White outline + lens highlight
    tft.drawRoundRect(EYE_L_X - 32, EYE_Y - 16, 64, 32, 8, C_WHITE);
    tft.drawRoundRect(EYE_R_X - 32, EYE_Y - 16, 64, 32, 8, C_WHITE);
    tft.drawLine(EYE_L_X - 22, EYE_Y - 10, EYE_L_X - 4, EYE_Y - 10, C_WHITE);
    tft.drawLine(EYE_R_X - 22, EYE_Y - 10, EYE_R_X - 4, EYE_Y - 10, C_WHITE);
}

// ─── Render ─────────────────────────────────────────────────────────────
static void render() {
    uint16_t bg = C_BLACK;
    switch (s_face) {
        case F_HAPPY:        bg = 0x0220; break;
        case F_EXCITED:      bg = 0x6209; break;   // warm pink/purple
        case F_SAD:          bg = 0x0008; break;
        case F_CRY:          bg = 0x0010; break;
        case F_ANGRY:        bg = 0x2000; break;
        case F_TABLE_FLIP:   bg = 0x2000; break;
        case F_LOVE:         bg = 0x4924; break;
        case F_WALK:         bg = 0x0010; break;
        case F_RUN:          bg = 0x2104; break;   // warmer for running
        case F_EMBARRASSED:  bg = 0x3000; break;
        case F_DIZZY:        bg = 0x2105; break;
        case F_SURPRISED:    bg = 0x0410; break;
        case F_COOL:         bg = 0x10a2; break;   // dark teal
        case F_WINK:         bg = 0x0220; break;
        default:             bg = C_BLACK;
    }
    tft.fillScreen(bg);

    if (s_blink_on) {
        eye_closed(EYE_L_X, EYE_Y);
        eye_closed(EYE_R_X, EYE_Y);
        return;
    }

    // Yawn overrides the usual IDLE rendering
    if (s_face == F_IDLE && s_yawn_phase != 0) {
        int progress = 0;
        if (s_yawn_phase == 1) progress = 100;        // opening — assume fully open by render
        else if (s_yawn_phase == 2) progress = 100;   // hold
        else if (s_yawn_phase == 3) progress = 40;    // closing
        eye_yawn(EYE_L_X, EYE_Y, progress);
        eye_yawn(EYE_R_X, EYE_Y, progress);
        int mh = 14 + (progress * 10 / 100);
        mouth_open(SCR_W / 2, 135, 28, mh);
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
            eye_caret(EYE_L_X, EYE_Y);
            eye_caret(EYE_R_X, EYE_Y);
            mouth_omega(SCR_W / 2, 130);
            draw_blush(50, 110);
            draw_blush(SCR_W - 65, 110);
            break;

        case F_EXCITED:
            eye_star(EYE_L_X, EYE_Y);
            eye_star(EYE_R_X, EYE_Y);
            mouth_omega(SCR_W / 2, 130);
            draw_sparkles();
            break;

        case F_SAD:
            eye_arc_down(EYE_L_X, EYE_Y - 5);
            eye_arc_down(EYE_R_X, EYE_Y - 5);
            mouth_frown(SCR_W / 2, 125, 38, 12);
            break;

        case F_CRY:
            eye_t(EYE_L_X, EYE_Y);
            eye_t(EYE_R_X, EYE_Y);
            mouth_line(SCR_W / 2, 135, 22);
            draw_tears(EYE_L_X, EYE_Y);
            draw_tears(EYE_R_X, EYE_Y);
            break;

        case F_ANGRY:
            eye_glare(EYE_L_X, EYE_Y);
            eye_glare(EYE_R_X, EYE_Y);
            mouth_gritted(SCR_W / 2, 125, 50, 12);
            break;

        case F_LOVE:
            eye_heart(EYE_L_X, EYE_Y - 4);
            eye_heart(EYE_R_X, EYE_Y - 4);
            mouth_smile(SCR_W / 2, 130, 22, 6, C_PINK);
            draw_sparkles();
            break;

        case F_SLEEP:
            eye_closed(EYE_L_X, EYE_Y, 30);
            eye_closed(EYE_R_X, EYE_Y, 30);
            mouth_line(SCR_W / 2, 130, 18, C_GRAY);
            draw_zzz();
            break;

        case F_SEARCH:
            eye_dot(EYE_L_X, EYE_Y, s_search_off, 0);
            eye_dot(EYE_R_X, EYE_Y, s_search_off, 0);
            mouth_o(SCR_W / 2, 130, 5);
            break;

        case F_CURIOUS:
            eye_question(EYE_L_X, EYE_Y);
            eye_question(EYE_R_X, EYE_Y);
            mouth_o(SCR_W / 2, 130, 4);
            break;

        case F_WALK: {
            int bounce = s_walk_phase ? 4 : -4;
            eye_dot(EYE_L_X, EYE_Y + bounce);
            eye_dot(EYE_R_X, EYE_Y + bounce);
            mouth_smile(SCR_W / 2, 130 + bounce, 25, 6);
            tft.fillRoundRect(SCR_W / 2 - 4, 132 + bounce, 8, 6, 3, C_RED);
            break;
        }

        case F_RUN: {
            int bounce = s_walk_phase ? 8 : -8;
            // wide bouncing eyes
            eye_dot(EYE_L_X, EYE_Y + bounce);
            eye_dot(EYE_R_X, EYE_Y + bounce);
            // panting open mouth with tongue
            mouth_open(SCR_W / 2, 132 + bounce, 32, 16);
            draw_tongue(SCR_W / 2, 136 + bounce, 14);
            // sweat drops
            draw_sweat(45,         55);
            draw_sweat(SCR_W - 45, 55);
            break;
        }

        case F_TABLE_FLIP:
            eye_glare(EYE_L_X + 10, EYE_Y + 10);
            eye_glare(EYE_R_X - 10, EYE_Y + 10);
            mouth_gritted(SCR_W / 2, 130, 50, 12);
            draw_arms_up();
            draw_motion_lines();
            draw_flying_table();
            break;

        case F_SURPRISED:
            eye_wide(EYE_L_X, EYE_Y);
            eye_wide(EYE_R_X, EYE_Y);
            mouth_o(SCR_W / 2, 138, 9);
            // small "!" floating
            tft.setTextColor(C_YELLOW);
            tft.setTextSize(3);
            tft.setCursor(SCR_W / 2 - 8, 18);
            tft.print("!");
            break;

        case F_COOL:
            draw_sunglasses();
            mouth_smirk(SCR_W / 2 - 4, 130);
            break;

        case F_EMBARRASSED:
            // half-closed eyes looking away
            eye_caret(EYE_L_X, EYE_Y);
            eye_caret(EYE_R_X, EYE_Y);
            mouth_o(SCR_W / 2, 130, 4);
            // blush slashes on each cheek
            draw_blush_slashes(35,         100);
            draw_blush_slashes(SCR_W - 60, 100);
            break;

        case F_DIZZY:
            eye_spiral(EYE_L_X, EYE_Y);
            eye_spiral(EYE_R_X, EYE_Y);
            mouth_wave(SCR_W / 2, 130);
            // little stars/birds around the head
            tft.setTextColor(C_YELLOW);
            tft.setTextSize(2);
            tft.setCursor(40, 20);    tft.print("*");
            tft.setCursor(SCR_W - 50, 20); tft.print("*");
            tft.setCursor(SCR_W / 2 - 4, 12); tft.print("*");
            break;

        case F_WINK:
            eye_caret(EYE_L_X, EYE_Y);
            eye_dot(EYE_R_X, EYE_Y);
            mouth_smile(SCR_W / 2, 130, 26, 7);
            // small heart sparkle near the wink
            tft.fillCircle(EYE_L_X + 22, EYE_Y - 18, 3, C_PINK);
            tft.fillCircle(EYE_L_X + 28, EYE_Y - 18, 3, C_PINK);
            tft.fillTriangle(EYE_L_X + 19, EYE_Y - 16, EYE_L_X + 31, EYE_Y - 16,
                             EYE_L_X + 25, EYE_Y - 8, C_PINK);
            break;

        case F_IDLE:
        default:
            eye_dot(EYE_L_X, EYE_Y, gx, gy);
            eye_dot(EYE_R_X, EYE_Y, gx, gy);
            mouth_line(SCR_W / 2, 130, 24);
            break;
    }
}

// ─── Names + parsing ────────────────────────────────────────────────────
static const char* face_name(FaceMode f) {
    switch (f) {
        case F_IDLE:        return "IDLE";
        case F_HAPPY:       return "HAPPY";
        case F_EXCITED:     return "EXCITED";
        case F_SAD:         return "SAD";
        case F_CRY:         return "CRY";
        case F_ANGRY:       return "ANGRY";
        case F_LOVE:        return "LOVE";
        case F_SLEEP:       return "SLEEP";
        case F_SEARCH:      return "SEARCH";
        case F_CURIOUS:     return "CURIOUS";
        case F_WALK:        return "WALK";
        case F_RUN:         return "RUN";
        case F_TABLE_FLIP:  return "TABLE_FLIP";
        case F_SURPRISED:   return "SURPRISED";
        case F_COOL:        return "COOL";
        case F_EMBARRASSED: return "EMBARRASSED";
        case F_DIZZY:       return "DIZZY";
        case F_WINK:        return "WINK";
        default:            return "?";
    }
}

static bool parse_face(const String& s, FaceMode& out) {
    if      (s == "IDLE")         out = F_IDLE;
    else if (s == "HAPPY")        out = F_HAPPY;
    else if (s == "EXCITED")      out = F_EXCITED;
    else if (s == "SAD")          out = F_SAD;
    else if (s == "CRY")          out = F_CRY;
    else if (s == "ANGRY")        out = F_ANGRY;
    else if (s == "LOVE")         out = F_LOVE;
    else if (s == "SLEEP")        out = F_SLEEP;
    else if (s == "SEARCH")       out = F_SEARCH;
    else if (s == "CURIOUS")      out = F_CURIOUS;
    else if (s == "WALK")         out = F_WALK;
    else if (s == "RUN")          out = F_RUN;
    else if (s == "TABLE_FLIP" || s == "FLIP") out = F_TABLE_FLIP;
    else if (s == "SURPRISED")    out = F_SURPRISED;
    else if (s == "COOL")         out = F_COOL;
    else if (s == "EMBARRASSED" || s == "SHY") out = F_EMBARRASSED;
    else if (s == "DIZZY")        out = F_DIZZY;
    else if (s == "WINK")         out = F_WINK;
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
        s_yawn_phase = 0;
        uint32_t now = millis();
        s_next_blink  = now + 2500 + random(0, 2000);
        s_next_glance = now + 3500 + random(0, 2500);
        s_next_yawn   = now + 15000 + random(0, 10000);
        s_next_search = now + 110;
        s_next_walk   = now + ((m == F_RUN) ? 100 : 200);
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

    // Serial1 for wired bot link (uncomment after wiring TX→RX):
    // Serial1.begin(115200, SERIAL_8N1, /*rx*/16, /*tx*/17);

    ledcAttach(TFT_BL, 5000, 8);
    ledcWrite(TFT_BL, 80);                   // ~30% brightness, runs cool

    SPI.begin(TFT_SCLK, /*MISO*/ -1, TFT_MOSI, TFT_CS);
    tft.init(SCR_H, SCR_W);
    tft.setRotation(1);
    tft.fillScreen(C_BLACK);
    tft.setTextColor(C_WHITE);

    render();

    uint32_t now = millis();
    s_next_blink  = now + 3000;
    s_next_glance = now + 4000;
    s_next_yawn   = now + 18000;
    s_next_search = now + 500;
    s_next_walk   = now + 200;

    Serial.println("ready — try: FACE:RUN  FACE:EXCITED  FACE:COOL  FACE:DIZZY  FACE:WINK");
}

void loop() {
    pump_serial(Serial);
    // pump_serial(Serial1);    // uncomment after wiring body TX → C6 RX

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
    if (glance_face && !s_blink_on && s_yawn_phase == 0 && now >= s_next_blink) {
        s_blink_on = true;
        s_blink_until = now + 130;
        render();
    }

    // Idle glance scheduler
    if (glance_face && !s_blink_on && s_yawn_phase == 0) {
        if (s_glance_x == 0 && s_glance_y == 0 && now >= s_next_glance) {
            static const int8_t DIRS[][2] = {
                {-12, 0}, {12, 0}, {0, -6}, {-10, -5}, {10, -5}, {-9, 5}, {9, 5},
            };
            int i = random(0, sizeof(DIRS) / sizeof(DIRS[0]));
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

    // Idle yawn scheduler — only when face = IDLE, not blinking, not glancing
    if (s_face == F_IDLE && !s_blink_on && s_glance_x == 0 && s_glance_y == 0) {
        if (s_yawn_phase == 0 && now >= s_next_yawn) {
            s_yawn_phase = 1;
            s_yawn_until = now + 400;
            render();
        } else if (s_yawn_phase == 1 && now >= s_yawn_until) {
            s_yawn_phase = 2;
            s_yawn_until = now + 600;
        } else if (s_yawn_phase == 2 && now >= s_yawn_until) {
            s_yawn_phase = 3;
            s_yawn_until = now + 250;
            render();
        } else if (s_yawn_phase == 3 && now >= s_yawn_until) {
            s_yawn_phase = 0;
            s_next_yawn = now + 18000 + random(0, 12000);
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

    // Walk / Run bounce
    if ((s_face == F_WALK || s_face == F_RUN) && !s_blink_on && now >= s_next_walk) {
        s_walk_phase ^= 1;
        s_next_walk = now + ((s_face == F_RUN) ? 100 : 200);
        render();
    }

    delay(8);
}
