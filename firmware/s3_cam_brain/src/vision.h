// Target: ESP32-S3-CAM (brain)
// OV2640 capture + simple detection. Today: camera init only — detection
// pipeline lands in Phase 2 of the plan.

#pragma once

#include <stdint.h>

namespace vision {

void init();
void update();   // call from loop()

bool          targetSeen();
int           targetX();
int           targetSize();
unsigned long millisSinceLastSeen();

}  // namespace vision
