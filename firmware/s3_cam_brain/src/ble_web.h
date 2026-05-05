// Target: ESP32-S3-CAM (brain)
// BLE Nordic UART Service + (optional) WiFi captive portal + web UI.
// All phone control flows through here and resolves to app_state setters.

#pragma once

#include <Arduino.h>

namespace ble_web {

void   init();
void   handleCommand(const String& cmd);   // shared by BLE RX and HTTP /cmd
void   send(const String& msg);            // back to phone over BLE NUS

}  // namespace ble_web
