// Target: ESP32-C6-LCD-1.47 (thin display client)
// Polls onboard / external buttons at 50 Hz with edge debouncing and
// emits PB_BTN_EVENT frames over the transport. Long-press fires after
// PB_INPUT_LONGPRESS_MS of continuous press.

#pragma once

#include <stdint.h>

#define PB_INPUT_POLL_HZ        50
#define PB_INPUT_DEBOUNCE_MS    20
#define PB_INPUT_LONGPRESS_MS   500

void input_init();
void input_poll();   // call from loop(); cheaply rate-limited internally
