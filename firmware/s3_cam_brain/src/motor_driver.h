// Target: ESP32-S3-CAM (brain)
// TB6612FNG (or DRV8833 / L298N) motor driver. Stubbed until pins on the
// specific S3-CAM carrier are locked in — set MOTORS_ENABLED to 1 and
// fill the TODOs in motor_driver.cpp.

#pragma once

namespace motor_driver {

void init();
void forward();
void back();
void left();
void right();
void stop();

bool enabled();   // true when MOTORS_ENABLED is set at compile time

}  // namespace motor_driver
