// Target: ESP32-C6-LCD-1.47 (thin display client)
// Abstract transport interface used to talk to the S3 brain. Same shape
// as the S3-side transport so main loops on both boards look identical.

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

Transport& transport();
