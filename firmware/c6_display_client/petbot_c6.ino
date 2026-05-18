// Auto-consolidated firmware for ESP32-C6-LCD-1.47 (display client)
// Generated from former src/*.cpp modules.

// Target: ESP32-C6-LCD-1.47 (thin display client)
// ST7789 driver wiring. Uses Arduino-ESP32's SPI with explicit pin mapping
// because the Waveshare board's MOSI/SCLK aren't the C6's default SPI
// pins.

#include "lcd.h"

#include <SPI.h>

static Adafruit_ST7789 g_lcd(&SPI, PB_LCD_PIN_CS, PB_LCD_PIN_DC, PB_LCD_PIN_RST);
static bool            g_inited = false;

void lcd_init() {
    if (g_inited) return;

    pinMode(PB_LCD_PIN_BL, OUTPUT);
    digitalWrite(PB_LCD_PIN_BL, HIGH);

    SPI.begin(PB_LCD_PIN_SCLK, /*MISO unused*/ -1, PB_LCD_PIN_MOSI, PB_LCD_PIN_CS);

    g_lcd.init(PB_LCD_H, PB_LCD_W);   // (height, width) at native rotation 0
    g_lcd.setRotation(1);             // landscape 320x172
    g_lcd.fillScreen(0x0000);
    g_lcd.setTextWrap(false);
    g_inited = true;
}

Adafruit_ST7789& lcd() { return g_lcd; }

void lcd_set_backlight(uint8_t level) {
    digitalWrite(PB_LCD_PIN_BL, level > 0 ? HIGH : LOW);
}

// Target: ESP32-C6-LCD-1.47 (thin display client)
// Streaming PNG receiver. The S3 emits PB_BLOB_PNG_BEGIN, then a series of
// PB_BLOB_PNG_CHUNK frames, then PB_BLOB_PNG_END. We accumulate chunks
// into a fixed SRAM buffer and decode on END.
//
// Behaviour today: SRAM only, hard cap = PB_PNG_SRAM_CAP. If the declared
// total exceeds the cap, the blob is dropped with a serial warning.
//
// TODO: SD-card fallback. The Waveshare ESP32-C6-LCD-1.47 has an SD slot
// — when total_len > PB_PNG_SRAM_CAP, open a temp file and stream the
// chunks to disk, then PNGdec.openFile() in png_blob_end(). Until that's
// wired up we deliberately fail loud (Serial warning) rather than fail
// silent.

#include "png_blob.h"

#include <Arduino.h>
#include <PNGdec.h>

#include "lcd.h"

#define PB_PNG_SRAM_CAP   (40 * 1024)

static uint8_t  s_buf[PB_PNG_SRAM_CAP];
static uint32_t s_total       = 0;
static uint32_t s_received    = 0;
static uint32_t s_expect_crc  = 0;
static uint16_t s_next_chunk  = 0;
static int16_t  s_blit_x      = 0;
static int16_t  s_blit_y      = 0;
static bool     s_active      = false;
static bool     s_overflowed  = false;

void png_blob_init() {
    s_total = s_received = 0;
    s_expect_crc = 0;
    s_next_chunk = 0;
    s_active = false;
    s_overflowed = false;
}

void png_blob_begin(uint32_t total_len, uint32_t crc32,
                    int16_t x, int16_t y, uint16_t /*w*/, uint16_t /*h*/) {
    s_total       = total_len;
    s_received    = 0;
    s_expect_crc  = crc32;
    s_next_chunk  = 0;
    s_blit_x      = x;
    s_blit_y      = y;
    s_active      = true;
    s_overflowed  = (total_len > PB_PNG_SRAM_CAP);
    if (s_overflowed) {
        Serial.printf("[png_blob] %lu B > SRAM cap %u B (TODO: SD fallback) - dropping\n",
                      (unsigned long)total_len, (unsigned)PB_PNG_SRAM_CAP);
    }
}

void png_blob_chunk(uint16_t chunk_idx, const uint8_t* data, uint16_t len) {
    if (!s_active || s_overflowed) return;
    if (chunk_idx != s_next_chunk) {
        Serial.printf("[png_blob] out-of-order chunk %u (expected %u) - dropping blob\n",
                      chunk_idx, s_next_chunk);
        s_active = false;
        return;
    }
    if (s_received + len > PB_PNG_SRAM_CAP || s_received + len > s_total) {
        Serial.println("[png_blob] chunk overflow - dropping blob");
        s_active = false;
        return;
    }
    memcpy(s_buf + s_received, data, len);
    s_received += len;
    s_next_chunk++;
}

// CRC32 (IEEE 802.3, poly 0xEDB88320), used to validate the assembled blob
// before handing it to PNGdec.
static uint32_t crc32_calc(const uint8_t* data, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= data[i];
        for (int k = 0; k < 8; ++k)
            c = (c >> 1) ^ (0xEDB88320u & -(int32_t)(c & 1));
    }
    return c ^ 0xFFFFFFFFu;
}

static int png_draw_callback(PNGDRAW* d) {
    // PNGdec gives us one scanline as RGB565 (after configuring the decoder
    // with PNG_PIXEL_RGB565). Push it as a window into the LCD at our blit
    // origin.
    static uint16_t line[PB_LCD_W];
    int n = d->iWidth > PB_LCD_W ? PB_LCD_W : d->iWidth;
    // PNG library writes RGB565 directly into pPixels when configured.
    auto& g = lcd();
    g.startWrite();
    g.setAddrWindow(s_blit_x, s_blit_y + d->y, n, 1);
    memcpy(line, d->pPixels, n * 2);
    g.writePixels(line, n);
    g.endWrite();
    return 1;
}

void png_blob_end() {
    if (!s_active) return;
    if (s_overflowed) { s_active = false; return; }

    if (s_received != s_total) {
        Serial.printf("[png_blob] received %lu / expected %lu - dropping\n",
                      (unsigned long)s_received, (unsigned long)s_total);
        s_active = false;
        return;
    }
    uint32_t got_crc = crc32_calc(s_buf, s_received);
    if (got_crc != s_expect_crc) {
        Serial.printf("[png_blob] CRC32 mismatch %08lx != %08lx - dropping\n",
                      (unsigned long)got_crc, (unsigned long)s_expect_crc);
        s_active = false;
        return;
    }

    PNG png;
    int rc = png.openRAM(s_buf, s_received, png_draw_callback);
    if (rc != PNG_SUCCESS) {
        Serial.printf("[png_blob] openRAM failed: %d\n", rc);
        s_active = false;
        return;
    }
    png.decode(nullptr, 0);
    png.close();
    s_active = false;
}

// Target: ESP32-C6-LCD-1.47 (thin display client)
// Translates wire-protocol frames into Adafruit_GFX primitive calls.

#include "renderer.h"

#include "lcd.h"
#include "png_blob.h"
#include "protocol/packets.h"

#include <Arduino.h>

static uint16_t rd_u16_be(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static int16_t  rd_i16_be(const uint8_t* p) { return (int16_t)rd_u16_be(p); }
static uint32_t rd_u32_be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static void handle_clear(const pb_frame_t& /*f*/) {
    lcd().fillScreen(0x0000);
}

// PB_DRAW_TEXT  payload: [x:i16][y:i16][color:u16][size:u8][text...]
static void handle_draw_text(const pb_frame_t& f) {
    if (f.len < 7) return;
    const uint8_t* p = f.payload;
    int16_t  x     = rd_i16_be(p + 0);
    int16_t  y     = rd_i16_be(p + 2);
    uint16_t color = rd_u16_be(p + 4);
    uint8_t  size  = p[6];
    auto& g = lcd();
    g.setCursor(x, y);
    g.setTextColor(color);
    g.setTextSize(size ? size : 1);
    for (uint16_t i = 7; i < f.len; ++i) g.write((char)p[i]);
}

// PB_DRAW_RECT  payload: [x:i16][y:i16][w:u16][h:u16][color:u16][filled:u8]
static void handle_draw_rect(const pb_frame_t& f) {
    if (f.len < 11) return;
    const uint8_t* p = f.payload;
    int16_t  x      = rd_i16_be(p + 0);
    int16_t  y      = rd_i16_be(p + 2);
    uint16_t w      = rd_u16_be(p + 4);
    uint16_t h      = rd_u16_be(p + 6);
    uint16_t color  = rd_u16_be(p + 8);
    bool     filled = p[10] != 0;
    if (filled) lcd().fillRect(x, y, w, h, color);
    else        lcd().drawRect(x, y, w, h, color);
}

// PB_DRAW_ICON  payload: [x:i16][y:i16][icon_id:u8]
// Today: render a small placeholder rect coloured by icon_id so the
// protocol is wired end-to-end. Real icon bitmaps land later when the
// asset pipeline does.
static void handle_draw_icon(const pb_frame_t& f) {
    if (f.len < 5) return;
    const uint8_t* p = f.payload;
    int16_t x = rd_i16_be(p + 0);
    int16_t y = rd_i16_be(p + 2);
    uint8_t id = p[4];
    static const uint16_t kPalette[8] = {
        0xFFFF, 0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF, 0xFC00,
    };
    lcd().fillRect(x, y, 16, 16, kPalette[id & 7]);
}

// PB_SET_MENU  payload:
//   [selected_idx:u8][title_len:u8][title...][n:u8] {item_len:u8 item...}*n
//
// Renders a simple title bar + vertical list. The selected item is
// inverse-coloured. Skip the LVGL path for now — Adafruit primitives are
// enough to ship and let the brain side stop pretending the menu is
// stateless.
static void handle_set_menu(const pb_frame_t& f) {
    if (f.len < 3) return;
    const uint8_t* p = f.payload;
    uint8_t selected  = p[0];
    uint8_t title_len = p[1];
    if ((uint16_t)2 + title_len + 1 > f.len) return;
    const uint8_t* title = p + 2;

    uint16_t off = 2 + title_len;
    uint8_t  n   = p[off++];

    auto& g = lcd();
    g.fillScreen(0x0000);

    // Title bar
    g.fillRect(0, 0, PB_LCD_W, 24, 0x4208);   // dark grey
    g.setTextColor(0xFFFF);
    g.setTextSize(2);
    g.setCursor(8, 4);
    for (uint8_t i = 0; i < title_len; ++i) g.write((char)title[i]);

    // Items
    g.setTextSize(2);
    int16_t y = 30;
    for (uint8_t i = 0; i < n; ++i) {
        if (off >= f.len) break;
        uint8_t item_len = p[off++];
        if (off + item_len > f.len) break;

        uint16_t bg = (i == selected) ? 0xF800 : 0x0000;
        uint16_t fg = (i == selected) ? 0xFFFF : 0xFFFF;
        g.fillRect(0, y - 2, PB_LCD_W, 22, bg);
        g.setTextColor(fg);
        g.setCursor(12, y + 2);
        for (uint8_t k = 0; k < item_len; ++k) g.write((char)p[off + k]);
        off += item_len;
        y += 24;
        if (y > PB_LCD_H - 20) break;
    }
}

// PB_BACKLIGHT  payload: [level:u8]
static void handle_backlight(const pb_frame_t& f) {
    if (f.len < 1) return;
    lcd_set_backlight(f.payload[0]);
}

// PB_BLOB_PNG_BEGIN  payload: [total_len:u32][crc32:u32][x:i16][y:i16][w:u16][h:u16]
static void handle_png_begin(const pb_frame_t& f) {
    if (f.len < 16) return;
    const uint8_t* p = f.payload;
    uint32_t total = rd_u32_be(p + 0);
    uint32_t crc32 = rd_u32_be(p + 4);
    int16_t  x     = rd_i16_be(p + 8);
    int16_t  y     = rd_i16_be(p + 10);
    uint16_t w     = rd_u16_be(p + 12);
    uint16_t h     = rd_u16_be(p + 14);
    png_blob_begin(total, crc32, x, y, w, h);
}

// PB_BLOB_PNG_CHUNK  payload: [chunk_idx:u16][data...]
static void handle_png_chunk(const pb_frame_t& f) {
    if (f.len < 2) return;
    const uint8_t* p = f.payload;
    uint16_t idx = rd_u16_be(p + 0);
    png_blob_chunk(idx, p + 2, f.len - 2);
}

static void handle_png_end(const pb_frame_t& /*f*/) {
    png_blob_end();
}

bool renderer_dispatch(const pb_frame_t& f) {
    switch (f.type) {
        case PB_CLEAR:           handle_clear(f);     return true;
        case PB_DRAW_TEXT:       handle_draw_text(f); return true;
        case PB_DRAW_RECT:       handle_draw_rect(f); return true;
        case PB_DRAW_ICON:       handle_draw_icon(f); return true;
        case PB_SET_MENU:        handle_set_menu(f);  return true;
        case PB_BACKLIGHT:       handle_backlight(f); return true;
        case PB_BLOB_PNG_BEGIN:  handle_png_begin(f); return true;
        case PB_BLOB_PNG_CHUNK:  handle_png_chunk(f); return true;
        case PB_BLOB_PNG_END:    handle_png_end(f);   return true;
        default:                                      return false;
    }
}

void renderer_init() {
    png_blob_init();
}

void renderer_show_waiting() {
    auto& g = lcd();
    g.fillScreen(0x0000);
    g.setTextColor(0xFFFF);
    g.setTextSize(2);
    g.setCursor(40, 70);
    g.print("PetBot");
    g.setTextSize(1);
    g.setCursor(40, 100);
    g.print("waiting for brain...");
}

// Target: ESP32-C6-LCD-1.47 (thin display client)
// Polls a small fixed table of button GPIOs and turns edges into protocol
// frames. Today only the BOOT button (GPIO9 on most C6 carriers) is
// listed. Add rows to kButtons[] when more buttons are wired — the rest
// is generic.

#include "input.h"

#include <Arduino.h>

#include "protocol/frame.h"
#include "protocol/packets.h"
#include "transport/transport.h"

struct ButtonCfg {
    uint8_t btn_id;
    int     pin;
    bool    active_low;
};

static const ButtonCfg kButtons[] = {
    // BOOT button — wired active-low on the Waveshare carrier.
    { PB_BTN_BOOT, 9, true },
    // Add more rows as you wire menu nav buttons (UP/DOWN/SELECT/BACK).
    // Avoid GPIOs 6, 7, 14, 15, 21, 22 (display) and 8 (RGB LED).
};

static constexpr size_t kNumButtons = sizeof(kButtons) / sizeof(kButtons[0]);

struct ButtonState {
    bool     pressed_stable;
    bool     last_raw;
    uint32_t last_change_ms;
    uint32_t press_started_ms;
    bool     longpress_fired;
};

static ButtonState s_state[kNumButtons];
static uint32_t    s_last_poll_ms = 0;
static uint8_t     s_seq          = 0;

static void emit_btn(uint8_t btn_id, uint8_t edge) {
    uint8_t enc[PB_FRAME_OVERHEAD + 2];
    uint8_t payload[2] = { btn_id, edge };
    size_t n = pb_encode(enc, sizeof(enc), PB_BTN_EVENT, s_seq++, payload, 2);
    if (n > 0) transport().write(enc, n);
}

void input_init() {
    for (size_t i = 0; i < kNumButtons; ++i) {
        pinMode(kButtons[i].pin, kButtons[i].active_low ? INPUT_PULLUP : INPUT_PULLDOWN);
        s_state[i] = ButtonState{ false, false, 0, 0, false };
    }
}

void input_poll() {
    uint32_t now = millis();
    if (now - s_last_poll_ms < (1000 / PB_INPUT_POLL_HZ)) return;
    s_last_poll_ms = now;

    for (size_t i = 0; i < kNumButtons; ++i) {
        const auto& cfg = kButtons[i];
        auto&       st  = s_state[i];

        bool raw  = digitalRead(cfg.pin) == LOW;
        if (!cfg.active_low) raw = !raw;

        if (raw != st.last_raw) {
            st.last_raw = raw;
            st.last_change_ms = now;
            continue;  // wait for debounce window
        }
        if (now - st.last_change_ms < PB_INPUT_DEBOUNCE_MS) continue;

        if (raw && !st.pressed_stable) {
            st.pressed_stable = true;
            st.press_started_ms = now;
            st.longpress_fired = false;
            emit_btn(cfg.btn_id, PB_BTN_PRESS);
        } else if (!raw && st.pressed_stable) {
            st.pressed_stable = false;
            emit_btn(cfg.btn_id, PB_BTN_RELEASE);
        } else if (raw && st.pressed_stable && !st.longpress_fired
                   && (now - st.press_started_ms >= PB_INPUT_LONGPRESS_MS)) {
            st.longpress_fired = true;
            emit_btn(cfg.btn_id, PB_BTN_LONGPRESS);
        }
    }
}

// Target: ESP32-C6-LCD-1.47 (thin display client)
// UART implementation. Avoid the reserved display GPIOs (6, 7, 14, 15,
// 21, 22) and the BOOT button (typically GPIO9); pin selection is provided
// by the constructor in the transport singleton.

#include "transport_uart.h"

TransportUart::TransportUart(HardwareSerial& port, int rx_pin, int tx_pin,
                             uint32_t baud)
    : port_(port), rx_pin_(rx_pin), tx_pin_(tx_pin), baud_(baud) {}

bool TransportUart::begin() {
    port_.begin(baud_, SERIAL_8N1, rx_pin_, tx_pin_);
    started_ = true;
    return true;
}

size_t TransportUart::write(const uint8_t* data, size_t len) {
    return started_ ? port_.write(data, len) : 0;
}

int TransportUart::read() {
    if (!started_ || port_.available() == 0) return -1;
    return port_.read();
}

size_t TransportUart::available() {
    return started_ ? port_.available() : 0;
}

// Target: ESP32-C6-LCD-1.47 (thin display client)
//
// TODO: USB CDC DEVICE transport — STUBBED until the UART path is fully working.
//
// The C6-LCD-1.47 ships with USB CDC On Boot enabled by default in the
// Waveshare/Arduino-ESP32 toolchain, which means `Serial` already maps to
// the native USB CDC port out of the box. Once we are ready to switch
// transports, the C6 side becomes a one-liner: this class wraps the
// global `Serial` object exactly the way TransportUart wraps Serial1.
//
// We are not enabling that yet because:
//   - The S3-side host implementation is also stubbed (see the matching
//     section in firmware/s3_cam_brain/petbot_s3.ino).
//     Bringing up only one half achieves nothing.
//   - The handshake (PB_HELLO from the C6, PB_SET_MENU response from the
//     S3) needs the full BRINGUP.md checklist re-run end-to-end on USB
//     before we can call this milestone done. That's Task 8 in the
//     working task list, after Task 7 passes.
//
// When implementing:
//   - In begin():    while (!Serial) yield();   // wait for host enumeration
//                    return true;
//   - In write():    return Serial.write(data, len);
//   - In read():     return Serial.available() ? Serial.read() : -1;
//   - In available():return Serial.available();
//   - Make sure platformio.ini sets `build_flags = -D ARDUINO_USB_CDC_ON_BOOT=1`
//     for this env (it is the default on the C6-LCD-1.47 but worth pinning
//     explicitly so the build doesn't drift).

#include "transport_usbcdc.h"

bool TransportUsbCdc::begin() {
    return false;
}

// Target: ESP32-C6-LCD-1.47 (thin display client)
// Build-flag-selected transport singleton.

#include "transport.h"

#if defined(PB_TRANSPORT_USBCDC) && PB_TRANSPORT_USBCDC
  #include "transport_usbcdc.h"
  static TransportUsbCdc g_transport;
#else
  // UART defaults for the C6 ↔ S3-CAM link.
  //
  // GPIOs 6, 7, 14, 15, 21, 22 are wired on-board to the ST7789 — do NOT
  // reuse. GPIO 9 is the BOOT button on most C6-LCD-1.47 boards. GPIO 8
  // is often the WS2812 RGB LED. The defaults below pick from the
  // remaining safe range; verify on your specific carrier.
  #include "transport_uart.h"
  static TransportUart g_transport(Serial1, /*rx*/16, /*tx*/17, 921600);
#endif

Transport& transport() { return g_transport; }

// Target: ESP32-C6-LCD-1.47 (thin display client)
// Entry point. Boots the LCD, opens the transport, sends PB_HELLO, then
// loops: drain transport bytes into the protocol decoder; on each full
// frame call renderer_dispatch(); poll buttons and emit PB_BTN_EVENT.

#include <Arduino.h>

#include "input.h"
#include "lcd.h"
#include "renderer.h"
#include "protocol/frame.h"
#include "protocol/packets.h"
#include "transport/transport.h"

#define PB_C6_FW_VERSION  0x0001
#define PB_C6_CAPS        0x0001  // bit 0 = ST7789 ready

static uint8_t      s_decoder_buf[PB_MAX_PAYLOAD];
static pb_decoder_t s_decoder;
static uint8_t      s_seq = 0;

static void send_hello() {
    uint8_t enc[PB_FRAME_OVERHEAD + 4];
    uint8_t payload[4] = {
        (uint8_t)(PB_C6_FW_VERSION >> 8), (uint8_t)(PB_C6_FW_VERSION & 0xFF),
        (uint8_t)(PB_C6_CAPS        >> 8), (uint8_t)(PB_C6_CAPS        & 0xFF),
    };
    size_t n = pb_encode(enc, sizeof(enc), PB_HELLO, s_seq++, payload, 4);
    if (n > 0) transport().write(enc, n);
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== PetBot C6 client booting ===");

    lcd_init();
    renderer_init();
    renderer_show_waiting();

    if (!transport().begin()) {
        Serial.println("[transport] begin() FAILED — check PB_TRANSPORT_* build flags");
    }

    pb_decoder_init(&s_decoder, s_decoder_buf, sizeof(s_decoder_buf));
    input_init();

    send_hello();
    Serial.println("=== PetBot C6 client ready ===");
}

void loop() {
    while (transport().available() > 0) {
        int b = transport().read();
        if (b < 0) break;
        pb_frame_t frame{};
        pb_status_t s = pb_feed(&s_decoder, (uint8_t)b, &frame);
        if (s == PB_OK) {
            if (!renderer_dispatch(frame)) {
                Serial.printf("[c6] unknown packet type 0x%02X len=%u\n",
                              frame.type, (unsigned)frame.len);
            }
        } else if (s == PB_ERR_CRC) {
            Serial.println("[c6] frame dropped: CRC mismatch");
        } else if (s == PB_ERR_LEN) {
            Serial.println("[c6] frame dropped: length > buffer cap");
        }
    }

    input_poll();
}
