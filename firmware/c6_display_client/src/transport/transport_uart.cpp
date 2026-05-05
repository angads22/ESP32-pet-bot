// Target: ESP32-C6-LCD-1.47 (thin display client)
// UART implementation. Avoid the reserved display GPIOs (6, 7, 14, 15,
// 21, 22) and the BOOT button (typically GPIO9); transport.cpp owns the
// pin selection.

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
