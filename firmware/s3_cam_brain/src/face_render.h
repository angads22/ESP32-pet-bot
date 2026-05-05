// Target: ESP32-S3-CAM (brain)
// Big face TFT (separate SPI bus from the OV2640 camera). Renders the
// current FaceMode and runs the blink animation in update().

#pragma once

#include "app_state.h"

namespace face_render {

void init();
void update();                          // call from loop() — drives blinks
void onExpressionChanged(FaceMode m);   // notified by app_state
bool enabled();

}  // namespace face_render
