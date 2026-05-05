// Target: ESP32-S3-CAM (brain)
// Motor stubs. To wire real motors:
//   1. Set MOTORS_ENABLED to 1.
//   2. Pick TB6612FNG-compatible GPIOs (avoid camera bus, face-TFT bus,
//      and the C6 transport pins from transport.cpp).
//   3. Fill the TODO bodies.

#include "motor_driver.h"

#include <Arduino.h>

#define MOTORS_ENABLED 0

namespace motor_driver {

bool enabled() { return MOTORS_ENABLED != 0; }

void init() {
#if MOTORS_ENABLED
    // TODO: pinMode(M_AIN1, OUTPUT); ... ledcAttach for PWMA/PWMB; STBY high.
#endif
}

void forward() {
    Serial.println("[motor] forward");
#if MOTORS_ENABLED
    // TODO: drive both motors forward at cruising PWM.
#endif
}

void back() {
    Serial.println("[motor] back");
#if MOTORS_ENABLED
    // TODO
#endif
}

void left() {
    Serial.println("[motor] left");
#if MOTORS_ENABLED
    // TODO
#endif
}

void right() {
    Serial.println("[motor] right");
#if MOTORS_ENABLED
    // TODO
#endif
}

void stop() {
    Serial.println("[motor] stop");
#if MOTORS_ENABLED
    // TODO: set both motor PWMs to 0; pull dir pins low.
#endif
}

}  // namespace motor_driver
