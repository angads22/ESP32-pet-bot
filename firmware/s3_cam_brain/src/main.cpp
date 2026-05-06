// Target: ESP32-S3-CAM (brain)
// Slim entry point. setup() boots each subsystem in dependency order;
// loop() pulls bytes from the C6 transport, dispatches frames, and ticks
// the periodic update() hooks. All command surfaces (BLE, web, C6 menu)
// resolve through app_state — no module owns its own copy of state.

#include <Arduino.h>

#include "app_state.h"
#include "audio_player.h"
#include "ble_web.h"
#include "face_render.h"
#include "menu_controller.h"
#include "motor_driver.h"
#include "vision.h"
#include "protocol/frame.h"
#include "protocol/packets.h"
#include "transport/transport.h"

static uint8_t      s_decoder_buf[PB_MAX_PAYLOAD];
static pb_decoder_t s_decoder;

static void on_frame(const pb_frame_t& f) {
    switch (f.type) {
        case PB_HELLO:
            menu_controller::onHelloFromC6();
            break;
        case PB_BTN_EVENT:
            if (f.len >= 2) menu_controller::onButtonEvent(f.payload[0], f.payload[1]);
            break;
        case PB_LOG: {
            uint8_t lvl = (f.len >= 1) ? f.payload[0] : 0;
            Serial.printf("[c6][lvl=%u] ", lvl);
            for (uint16_t i = 1; i < f.len; ++i) Serial.write((char)f.payload[i]);
            Serial.println();
            break;
        }
        case PB_ACK:
        case PB_NAK:
            // Reserved for future flow control — log and ignore for now.
            break;
        default:
            Serial.printf("[s3] unknown C6 packet 0x%02X len=%u\n", f.type, (unsigned)f.len);
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== PetBot S3 brain booting ===");

    app_state::init();
    motor_driver::init();
    audio_player::init();
    face_render::init();
    vision::init();

    if (!transport().begin()) {
        Serial.println("[transport] begin() FAILED — check transport.cpp build flag");
    }
    pb_decoder_init(&s_decoder, s_decoder_buf, sizeof(s_decoder_buf));

    menu_controller::init();
    ble_web::init();

    Serial.println("=== PetBot S3 brain ready ===");
}

void loop() {
    while (transport().available() > 0) {
        int b = transport().read();
        if (b < 0) break;
        pb_frame_t frame{};
        pb_status_t s = pb_feed(&s_decoder, (uint8_t)b, &frame);
        if (s == PB_OK) {
            on_frame(frame);
        } else if (s == PB_ERR_CRC) {
            Serial.println("[s3] frame dropped: CRC mismatch");
        } else if (s == PB_ERR_LEN) {
            Serial.println("[s3] frame dropped: length > buffer cap");
        }
    }

    face_render::update();
    vision::update();
    menu_controller::update();
    delay(2);
}
