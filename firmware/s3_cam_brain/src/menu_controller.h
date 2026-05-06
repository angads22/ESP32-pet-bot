// Target: ESP32-S3-CAM (brain)
// Builds the C6 menu tree on the S3, emits PB_SET_MENU packets, and
// translates incoming PB_BTN_EVENT packets into navigation. All menu
// actions resolve to app_state setters so menu == BLE == one code path.

#pragma once

#include <stdint.h>

namespace menu_controller {

void init();
void update();                                  // periodic — currently no-op

void onHelloFromC6();                           // arm: push the root menu
void onButtonEvent(uint8_t btn_id, uint8_t edge);
void onStateChanged();                          // re-render after app_state change

// Push a one-line debug status line to the C6 (used by the SCREEN: BLE verb).
void pushDebugLine(const char* text);

}  // namespace menu_controller
