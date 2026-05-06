// Target: ESP32-C6-LCD-1.47 (thin display client)
// UART implementation of the Transport interface.

#pragma once

#include <Arduino.h>

#include "transport.h"

class TransportUart : public Transport {
public:
    TransportUart(HardwareSerial& port, int rx_pin, int tx_pin, uint32_t baud);
    bool   begin() override;
    size_t write(const uint8_t* data, size_t len) override;
    int    read() override;
    size_t available() override;

private:
    HardwareSerial& port_;
    int      rx_pin_;
    int      tx_pin_;
    uint32_t baud_;
    bool     started_ = false;
};
