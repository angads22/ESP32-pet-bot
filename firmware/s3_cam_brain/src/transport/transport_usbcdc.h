// Target: ESP32-S3-CAM (brain)
// USB CDC HOST stub. See the USB CDC section in
// firmware/s3_cam_brain/petbot_s3.ino for the full caveat list and why
// this is intentionally not implemented yet.

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
