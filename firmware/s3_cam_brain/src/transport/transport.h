// Target: ESP32-S3-CAM (brain)
// Abstract transport interface used to talk to the C6 display client.
// Compile-time pick between TransportUart (default) and TransportUsbCdc
// (stubbed) via PB_TRANSPORT_UART / PB_TRANSPORT_USBCDC build flags.

#pragma once

#include <stddef.h>
#include <stdint.h>

class Transport {
public:
    virtual ~Transport() = default;
    virtual bool   begin() = 0;
    virtual size_t write(const uint8_t* data, size_t len) = 0;
    virtual int    read() = 0;          // -1 if nothing ready
    virtual size_t available() = 0;
};

// Returns the single, build-flag-selected transport instance.
Transport& transport();
