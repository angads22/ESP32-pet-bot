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
        Serial.println("[transport] begin() FAILED — check transport.cpp build flag");
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
