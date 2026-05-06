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
