// PetBot wire protocol — framing, CRC, streaming decoder
// Target: shared between ESP32-S3-CAM and ESP32-C6-LCD-1.47.
// Compiles cleanly with g++ on the host for unit tests.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "packets.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PB_NEED_MORE = 0,  // not a full frame yet — keep feeding
    PB_OK        = 1,  // valid frame; populated in *out
    PB_ERR_CRC   = 2,  // frame complete but CRC mismatch — discarded, decoder reset
    PB_ERR_LEN   = 3,  // declared length > buffer cap — discarded, decoder reset
} pb_status_t;

typedef struct {
    uint8_t        type;
    uint8_t        seq;
    uint16_t       len;
    const uint8_t* payload;  // points into the decoder's buffer; consume immediately
} pb_frame_t;

typedef struct {
    uint8_t  state;
    uint8_t  type;
    uint8_t  seq;
    uint16_t len;
    uint16_t bytes_read;
    uint16_t crc_received;
    uint8_t* buffer;       // user-supplied payload buffer
    uint16_t buffer_cap;
} pb_decoder_t;

// Init decoder with caller-owned payload buffer. `cap` should be ≥ the
// largest payload you expect to receive (PB_MAX_PAYLOAD is the protocol's
// hard cap).
void   pb_decoder_init(pb_decoder_t* d, uint8_t* buf, uint16_t cap);
void   pb_decoder_reset(pb_decoder_t* d);

// Encode one frame into `out`. Returns total bytes written (always
// `len + PB_FRAME_OVERHEAD`) on success, or 0 if `out_cap` is too small or
// `len > PB_MAX_PAYLOAD`.
size_t pb_encode(uint8_t* out, size_t out_cap,
                 uint8_t type, uint8_t seq,
                 const uint8_t* payload, uint16_t len);

// Streaming decode: feed one byte. Returns PB_NEED_MORE while a frame is
// being assembled, then PB_OK / PB_ERR_CRC / PB_ERR_LEN at the trailing
// CRC byte. After any non-NEED_MORE return the decoder is implicitly reset
// and ready for the next frame. `out->payload` is valid only until the
// next call to pb_feed().
pb_status_t pb_feed(pb_decoder_t* d, uint8_t byte, pb_frame_t* out);

// CRC16-CCITT (poly 0x1021, init 0xFFFF, no XOR-out, no reflection).
uint16_t pb_crc16_ccitt(const uint8_t* data, size_t n);
uint16_t pb_crc16_ccitt_update(uint16_t crc, const uint8_t* data, size_t n);

#ifdef __cplusplus
}
#endif
