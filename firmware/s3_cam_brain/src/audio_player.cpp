// Target: ESP32-S3-CAM (brain)
// Audio stubs. Set SPEAKER_ENABLED 1 once the I2S pins for MAX98357A are
// known — BCLK, LRC, DIN. Picking pins after the camera bus is finalised
// avoids reshuffling later.

#include "audio_player.h"

#include <Arduino.h>

#define SPEAKER_ENABLED 0

namespace audio_player {

bool enabled() { return SPEAKER_ENABLED != 0; }

void init() {
#if SPEAKER_ENABLED
    // TODO: i2s_driver_install + pin config.
#endif
}

void say(const char* text) {
    Serial.print("[say] "); Serial.println(text ? text : "");
#if SPEAKER_ENABLED
    // TODO: feed synth output into i2s_write.
#endif
}

void play(const char* name) {
    Serial.print("[sound] "); Serial.println(name ? name : "");
#if SPEAKER_ENABLED
    // TODO: look up `name` in the sound bank and play it.
#endif
}

}  // namespace audio_player
