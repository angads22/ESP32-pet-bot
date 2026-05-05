// PetBot wire protocol — framing + CRC + streaming decoder.
// Target: shared between ESP32-S3-CAM, ESP32-C6-LCD-1.47, and host tests.
// Implementation has no Arduino / FreeRTOS dependencies; pure C with a
// .cpp extension so PlatformIO picks it up uniformly.

#include "frame.h"

#include <string.h>

uint16_t pb_crc16_ccitt_update(uint16_t crc, const uint8_t* data, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        crc ^= ((uint16_t)data[i]) << 8;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                 : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint16_t pb_crc16_ccitt(const uint8_t* data, size_t n) {
    return pb_crc16_ccitt_update(0xFFFF, data, n);
}

size_t pb_encode(uint8_t* out, size_t out_cap,
                 uint8_t type, uint8_t seq,
                 const uint8_t* payload, uint16_t len) {
    if (len > PB_MAX_PAYLOAD) return 0;
    size_t total = (size_t)len + PB_FRAME_OVERHEAD;
    if (out_cap < total) return 0;

    out[0] = PB_MAGIC0;
    out[1] = PB_MAGIC1;
    out[2] = type;
    out[3] = seq;
    out[4] = (uint8_t)(len >> 8);
    out[5] = (uint8_t)(len & 0xFF);
    if (len) memcpy(&out[6], payload, len);

    uint16_t crc = pb_crc16_ccitt(&out[2], 4 + (size_t)len);
    out[6 + len]     = (uint8_t)(crc >> 8);
    out[7 + len]     = (uint8_t)(crc & 0xFF);
    return total;
}

// ─── Streaming decoder ───────────────────────────────────────────────────────
enum {
    PB_S_M0 = 0,
    PB_S_M1,
    PB_S_TYPE,
    PB_S_SEQ,
    PB_S_LEN_HI,
    PB_S_LEN_LO,
    PB_S_PAYLOAD,
    PB_S_CRC_HI,
    PB_S_CRC_LO,
};

void pb_decoder_init(pb_decoder_t* d, uint8_t* buf, uint16_t cap) {
    d->buffer = buf;
    d->buffer_cap = cap;
    pb_decoder_reset(d);
}

void pb_decoder_reset(pb_decoder_t* d) {
    d->state = PB_S_M0;
    d->type = 0;
    d->seq = 0;
    d->len = 0;
    d->bytes_read = 0;
    d->crc_received = 0;
}

pb_status_t pb_feed(pb_decoder_t* d, uint8_t byte, pb_frame_t* out) {
    switch (d->state) {
        case PB_S_M0:
            if (byte == PB_MAGIC0) d->state = PB_S_M1;
            return PB_NEED_MORE;

        case PB_S_M1:
            if (byte == PB_MAGIC1) {
                d->state = PB_S_TYPE;
            } else if (byte == PB_MAGIC0) {
                // 0xAA 0xAA — keep waiting for 0x55, treat the second 0xAA as
                // a fresh resync candidate.
            } else {
                d->state = PB_S_M0;
            }
            return PB_NEED_MORE;

        case PB_S_TYPE:
            d->type = byte;
            d->state = PB_S_SEQ;
            return PB_NEED_MORE;

        case PB_S_SEQ:
            d->seq = byte;
            d->state = PB_S_LEN_HI;
            return PB_NEED_MORE;

        case PB_S_LEN_HI:
            d->len = ((uint16_t)byte) << 8;
            d->state = PB_S_LEN_LO;
            return PB_NEED_MORE;

        case PB_S_LEN_LO:
            d->len |= byte;
            d->bytes_read = 0;
            if (d->len > d->buffer_cap || d->len > PB_MAX_PAYLOAD) {
                pb_decoder_reset(d);
                return PB_ERR_LEN;
            }
            d->state = (d->len == 0) ? PB_S_CRC_HI : PB_S_PAYLOAD;
            return PB_NEED_MORE;

        case PB_S_PAYLOAD:
            d->buffer[d->bytes_read++] = byte;
            if (d->bytes_read >= d->len) d->state = PB_S_CRC_HI;
            return PB_NEED_MORE;

        case PB_S_CRC_HI:
            d->crc_received = ((uint16_t)byte) << 8;
            d->state = PB_S_CRC_LO;
            return PB_NEED_MORE;

        case PB_S_CRC_LO: {
            d->crc_received |= byte;
            uint8_t hdr[4] = {
                d->type, d->seq,
                (uint8_t)(d->len >> 8), (uint8_t)(d->len & 0xFF),
            };
            uint16_t c = pb_crc16_ccitt_update(0xFFFF, hdr, 4);
            c = pb_crc16_ccitt_update(c, d->buffer, d->len);
            if (c != d->crc_received) {
                pb_decoder_reset(d);
                return PB_ERR_CRC;
            }
            out->type    = d->type;
            out->seq     = d->seq;
            out->len     = d->len;
            out->payload = d->buffer;
            // Reset just the FSM — leave d->buffer contents intact until the
            // caller consumes them (by returning PB_OK we promise the payload
            // pointer is valid right now).
            d->state = PB_S_M0;
            d->bytes_read = 0;
            d->crc_received = 0;
            return PB_OK;
        }

        default:
            pb_decoder_reset(d);
            return PB_NEED_MORE;
    }
}
