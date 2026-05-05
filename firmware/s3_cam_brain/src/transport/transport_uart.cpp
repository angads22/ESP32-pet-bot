// Target: ESP32-S3-CAM (brain)
// UART implementation. Uses HardwareSerial1 by convention; pins and baud
// come from the constructor so transport.cpp owns the policy choice.

#include "transport_uart.h"

TransportUart::TransportUart(HardwareSerial& port, int rx_pin, int tx_pin,
                             uint32_t baud)
    : port_(port), rx_pin_(rx_pin), tx_pin_(tx_pin), baud_(baud) {}

bool TransportUart::begin() {
    port_.begin(baud_, SERIAL_8N1, rx_pin_, tx_pin_);
    started_ = true;
    return true;
}

size_t TransportUart::write(const uint8_t* data, size_t len) {
    return started_ ? port_.write(data, len) : 0;
}

int TransportUart::read() {
    if (!started_ || port_.available() == 0) return -1;
    return port_.read();
}

size_t TransportUart::available() {
    return started_ ? port_.available() : 0;
}
