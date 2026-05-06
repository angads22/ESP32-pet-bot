// Target: ESP32-C6-LCD-1.47 (thin display client)
// ST7789 init + accessor. Uses the Waveshare-fixed display GPIOs.

#pragma once

#include <Adafruit_ST7789.h>

// Reserved Waveshare ESP32-C6-LCD-1.47 display pins — DO NOT REUSE.
#define PB_LCD_PIN_MOSI  6
#define PB_LCD_PIN_SCLK  7
#define PB_LCD_PIN_CS    14
#define PB_LCD_PIN_DC    15
#define PB_LCD_PIN_RST   21
#define PB_LCD_PIN_BL    22

// 172x320 native ST7789. After setRotation(1) the usable area is 320x172
// landscape (matches the renderFace() math from the previous prototype).
#define PB_LCD_W         320
#define PB_LCD_H         172

// Init SPI, drive RST/BL, clear to black, setRotation(1). Idempotent —
// safe to call once at boot.
void              lcd_init();

// Returns the global ST7789 driver. Renderer and PNG-blob both use this.
Adafruit_ST7789&  lcd();

// 0–255 backlight level. Currently a binary on/off via digitalWrite (the
// pin is a plain GPIO on this board); upgrade to PWM if you wire BL to a
// LEDC channel later.
void              lcd_set_backlight(uint8_t level);
