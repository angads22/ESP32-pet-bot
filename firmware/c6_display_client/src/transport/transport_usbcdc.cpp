// Target: ESP32-C6-LCD-1.47 (thin display client)
//
// TODO: USB CDC DEVICE transport — STUBBED until the UART path is fully working.
//
// The C6-LCD-1.47 ships with USB CDC On Boot enabled by default in the
// Waveshare/Arduino-ESP32 toolchain, which means `Serial` already maps to
// the native USB CDC port out of the box. Once we are ready to switch
// transports, the C6 side becomes a one-liner: this class wraps the
// global `Serial` object exactly the way TransportUart wraps Serial1.
//
// We are not enabling that yet because:
//   - The S3-side host implementation is also stubbed (see the matching
//     transport_usbcdc.cpp in firmware/s3_cam_brain/src/transport/).
//     Bringing up only one half achieves nothing.
//   - The handshake (PB_HELLO from the C6, PB_SET_MENU response from the
//     S3) needs the full BRINGUP.md checklist re-run end-to-end on USB
//     before we can call this milestone done. That's Task 8 in the
//     working task list, after Task 7 passes.
//
// When implementing:
//   - In begin():    while (!Serial) yield();   // wait for host enumeration
//                    return true;
//   - In write():    return Serial.write(data, len);
//   - In read():     return Serial.available() ? Serial.read() : -1;
//   - In available():return Serial.available();
//   - Make sure platformio.ini sets `build_flags = -D ARDUINO_USB_CDC_ON_BOOT=1`
//     for this env (it is the default on the C6-LCD-1.47 but worth pinning
//     explicitly so the build doesn't drift).

#include "transport_usbcdc.h"

bool TransportUsbCdc::begin() {
    return false;
}
