// Target: ESP32-S3-CAM (brain)
// Single source of truth for behaviour-affecting state. Both ble_web (phone
// commands) and menu_controller (C6 button events) call into this API —
// nobody touches the underlying motor / audio / face modules directly.

#pragma once

#include <stdint.h>

enum class FaceMode : uint8_t {
    IDLE = 0,
    HAPPY,
    SEARCH,
    CURIOUS,
    DRIVE,
    SLEEP,
    _COUNT,
};

enum class CtrlMode : uint8_t {
    MANUAL = 0,
    AUTO,
    _COUNT,
};

namespace app_state {

void init();

// Expression / mode setters — the canonical entry points.
void     setExpression(FaceMode m);
FaceMode expression();

void     setMode(CtrlMode m);
CtrlMode mode();

// Movement (manual control). In AUTO mode these may be ignored or
// overridden by the state machine; today they are passthroughs to
// motor_driver and the design allows extending later.
void moveForward();
void moveBack();
void moveLeft();
void moveRight();
void moveStop();

// Sound / TTS pass-throughs.
void say(const char* text);
void playSound(const char* name);

// String helpers — used for STATUS replies and for menu rendering.
const char* expressionName(FaceMode m);
const char* modeName(CtrlMode m);

}  // namespace app_state
