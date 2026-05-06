// Target: ESP32-S3-CAM (brain)
//
// TODO: USB CDC HOST transport — STUBBED until the UART path is fully working.
//
// On the S3 side this class will eventually drive a TinyUSB host stack and
// wrap the USBHostSerial class shipped in Arduino-ESP32 v3.x. It is NOT
// implemented yet because bring-up requires hardware verification that
// can't be done blind:
//
//   1. Confirm the specific S3-CAM carrier exposes a NATIVE USB port (the
//      D+/D- pins of the ESP32-S3 itself), in addition to any CH340/CP2102
//      UART-only port used for flashing. Some S3-CAM clones only expose
//      the UART bridge — those cannot be USB hosts.
//   2. Confirm the USB-C receptacle is wired for HOST mode: CC1 / CC2 each
//      pulled to GND through 5.1 kΩ. Without that, a USB-C cable plugged
//      into a device-mode peripheral will not negotiate.
//   3. Confirm the board can drive +5 V outward on VBUS. If it can't, a
//      USB-C-to-USB-A-host OTG adapter plus an A-to-C cable into the C6 is
//      required.
//
// If any of those is no, document it in HARDWARE.md and either keep using
// UART transport or fit the OTG adapter.
//
// When implementation begins:
//   - Switch the build env to enable TinyUSB host (`board_build.f_cpu`,
//     `build_flags = -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=0`,
//     plus the TinyUSB host include path).
//   - In begin():   USB.begin(); host_serial_.begin();
//   - In write():   forward bytes to host_serial_.write().
//   - In read():    pull from host_serial_ when host_serial_.available().
//   - In available(): host_serial_.available().
//
// Re-run BRINGUP.md from step 1 after switching transport. Until any of
// that is in place, begin() returns false so the firmware can fail fast
// rather than silently dropping frames.

#include "transport_usbcdc.h"

bool TransportUsbCdc::begin() {
    return false;
}
