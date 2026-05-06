// Target: ESP32-C6-LCD-1.47 (thin display client)
// Executes incoming protocol draw frames against the ST7789.

#pragma once

#include <stdint.h>

#include "protocol/frame.h"

void renderer_init();

// Dispatch a fully-decoded frame. Returns true if the frame was handled
// (including BLOB chunks routed to png_blob); false if the type is
// unknown.
bool renderer_dispatch(const pb_frame_t& f);

// Splash screen shown before the first frame from the S3 arrives.
void renderer_show_waiting();
