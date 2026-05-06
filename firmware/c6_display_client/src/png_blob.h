// Target: ESP32-C6-LCD-1.47 (thin display client)
// Accumulates BLOB_PNG_* chunks and decodes the result with PNGdec onto
// the ST7789. SRAM-only path today — SD-card fallback for blobs > 40 KB
// is stubbed (see TODO inside png_blob.cpp).

#pragma once

#include <stdint.h>

void png_blob_init();
void png_blob_begin(uint32_t total_len, uint32_t crc32,
                    int16_t x, int16_t y, uint16_t w, uint16_t h);
void png_blob_chunk(uint16_t chunk_idx, const uint8_t* data, uint16_t len);
void png_blob_end();
