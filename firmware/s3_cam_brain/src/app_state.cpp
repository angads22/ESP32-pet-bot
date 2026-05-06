// Target: ESP32-S3-CAM (brain)
// Holds expression / mode state and routes side effects to the relevant
// subsystem modules. Cross-module calls live here so menu_controller and
// ble_web don't have to know about each other.

#include "app_state.h"

#include "audio_player.h"
#include "face_render.h"
#include "menu_controller.h"
#include "motor_driver.h"

namespace app_state {

static FaceMode g_expr = FaceMode::IDLE;
static CtrlMode g_mode = CtrlMode::MANUAL;

void init() {
    g_expr = FaceMode::IDLE;
    g_mode = CtrlMode::MANUAL;
}

void setExpression(FaceMode m) {
    if (m == g_expr) return;
    g_expr = m;
    face_render::onExpressionChanged(m);
    menu_controller::onStateChanged();
}

FaceMode expression() { return g_expr; }

void setMode(CtrlMode m) {
    if (m == g_mode) return;
    g_mode = m;
    menu_controller::onStateChanged();
}

CtrlMode mode() { return g_mode; }

void moveForward() { motor_driver::forward(); }
void moveBack()    { motor_driver::back(); }
void moveLeft()    { motor_driver::left(); }
void moveRight()   { motor_driver::right(); }
void moveStop()    { motor_driver::stop(); }

void say(const char* text)       { audio_player::say(text); }
void playSound(const char* name) { audio_player::play(name); }

const char* expressionName(FaceMode m) {
    switch (m) {
        case FaceMode::IDLE:    return "IDLE";
        case FaceMode::HAPPY:   return "HAPPY";
        case FaceMode::SEARCH:  return "SEARCH";
        case FaceMode::CURIOUS: return "CURIOUS";
        case FaceMode::DRIVE:   return "DRIVE";
        case FaceMode::SLEEP:   return "SLEEP";
        default:                return "?";
    }
}

const char* modeName(CtrlMode m) {
    switch (m) {
        case CtrlMode::MANUAL:  return "manual";
        case CtrlMode::AUTO:    return "auto";
        default:                return "?";
    }
}

}  // namespace app_state
