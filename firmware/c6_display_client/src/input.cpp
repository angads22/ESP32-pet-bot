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
