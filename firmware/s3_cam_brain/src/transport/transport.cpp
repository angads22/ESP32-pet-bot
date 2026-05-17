// Target: ESP32-S3-CAM (brain)
// Build-flag-selected transport singleton. Choose UART or USB-CDC at
// compile time; main.cpp just calls transport().begin() / read() / write().

#include "transport.h"

#if defined(PB_TRANSPORT_USBCDC) && PB_TRANSPORT_USBCDC
  #include "transport_usbcdc.h"
  static TransportUsbCdc g_transport;
#else
  // UART pins for the S3 ↔ C6 link on the Freenove ESP32-S3 WROOM CAM.
  // GPIO 17 and 18 are camera D6 / D5 on this board — DO NOT use them.
  // Defaults below pick from the high-numbered free GPIOs per HARDWARE_MAP.md.
  // Override at link time by defining C6_LINK_TX / C6_LINK_RX in build_flags
  // or pin_config.h (Phase 1.2+).
  #include "transport_uart.h"
  #ifndef C6_LINK_TX
    #define C6_LINK_TX 38
  #endif
  #ifndef C6_LINK_RX
    #define C6_LINK_RX 39
  #endif
  static TransportUart g_transport(Serial1, /*rx*/C6_LINK_RX, /*tx*/C6_LINK_TX, 921600);
#endif

Transport& transport() { return g_transport; }
