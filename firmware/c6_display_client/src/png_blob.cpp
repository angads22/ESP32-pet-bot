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
