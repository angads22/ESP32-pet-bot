// Target: ESP32-C6-LCD-1.47 (thin display client)
// Build-flag-selected transport singleton.

#include "transport.h"

#if defined(PB_TRANSPORT_USBCDC) && PB_TRANSPORT_USBCDC
  #include "transport_usbcdc.h"
  static TransportUsbCdc g_transport;
#else
  // UART defaults for the C6 ↔ S3-CAM link.
  //
  // GPIOs 6, 7, 14, 15, 21, 22 are wired on-board to the ST7789 — do NOT
  // reuse. GPIO 9 is the BOOT button on most C6-LCD-1.47 boards. GPIO 8
  // is often the WS2812 RGB LED. The defaults below pick from the
  // remaining safe range; verify on your specific carrier.
  #include "transport_uart.h"
  static TransportUart g_transport(Serial1, /*rx*/16, /*tx*/17, 921600);
#endif

Transport& transport() { return g_transport; }
