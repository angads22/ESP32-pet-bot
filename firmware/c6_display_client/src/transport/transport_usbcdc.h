// Target: ESP32-C6-LCD-1.47 (thin display client)
// USB CDC DEVICE stub. See top of transport_usbcdc.cpp for the full TODO.

#pragma once

#include "transport.h"

class TransportUsbCdc : public Transport {
public:
    TransportUsbCdc() = default;
    bool   begin() override;
    size_t write(const uint8_t* /*data*/, size_t /*len*/) override { return 0; }
    int    read() override { return -1; }
    size_t available() override { return 0; }
};
