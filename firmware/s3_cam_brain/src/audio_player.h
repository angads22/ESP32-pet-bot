// Target: ESP32-S3-CAM (brain)
// MAX98357A I2S amp + sound bank. Stubbed until the I2S pins are wired.

#pragma once

namespace audio_player {

void init();
void say(const char* text);     // TTS — placeholder until a TTS lib lands
void play(const char* name);    // BOOT / HAPPY / ALERT etc.

bool enabled();

}  // namespace audio_player
