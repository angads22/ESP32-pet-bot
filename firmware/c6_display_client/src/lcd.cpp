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
