// Target: ESP32-S3-CAM (brain)
// Build-flag-selected transport singleton. Choose UART or USB-CDC at
// compile time; main.cpp just calls transport().begin() / read() / write().

#include "transport.h"

#if defined(PB_TRANSPORT_USBCDC) && PB_TRANSPORT_USBCDC
  #include "transport_usbcdc.h"
  static TransportUsbCdc g_transport;
#else
  // UART defaults for the S3-CAM ↔ C6 link.
  // RX = GPIO18, TX = GPIO17 (per ROBOT_FIRMWARE_PLAN.md §7).
  // Adjust pin numbers if your S3-CAM carrier conflicts.
  #include "transport_uart.h"
  static TransportUart g_transport(Serial1, /*rx*/18, /*tx*/17, 921600);
#endif

Transport& transport() { return g_transport; }
