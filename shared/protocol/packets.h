// PetBot wire protocol — packet IDs and constants
// Target: shared between ESP32-S3-CAM (brain) and ESP32-C6-LCD-1.47 (client).
// Pure C-compatible header: include from .c, .cpp, host-side tests, anywhere.

#pragma once

#include <stdint.h>

#define PB_MAGIC0 0xAA
#define PB_MAGIC1 0x55

// ─── S3 → C6 (display + control) ─────────────────────────────────────────────
#define PB_CLEAR            0x01
#define PB_DRAW_TEXT        0x02
#define PB_DRAW_RECT        0x03
#define PB_DRAW_ICON        0x04
#define PB_SET_MENU         0x05
#define PB_BLOB_PNG_BEGIN   0x06
#define PB_BLOB_PNG_CHUNK   0x07
#define PB_BLOB_PNG_END     0x08
#define PB_BACKLIGHT        0x09  // payload: [level:u8] (0–255)

// ─── C6 → S3 (input + acks + telemetry) ──────────────────────────────────────
#define PB_BTN_EVENT        0x80  // payload: [btn_id:u8][edge:u8]
#define PB_ACK              0x81  // payload: [ack_seq:u8]
#define PB_NAK              0x82  // payload: [ack_seq:u8][reason:u8]
#define PB_LOG              0x83  // payload: [level:u8][text...]
#define PB_HELLO            0x84  // payload: [fw_version:u16][caps:u16]

// Button edges (in PB_BTN_EVENT)
#define PB_BTN_RELEASE      0x00
#define PB_BTN_PRESS        0x01
#define PB_BTN_LONGPRESS    0x02

// Conventional button IDs — extend as more buttons are wired
#define PB_BTN_BOOT         0x00
#define PB_BTN_UP           0x01
#define PB_BTN_DOWN         0x02
#define PB_BTN_SELECT       0x03
#define PB_BTN_BACK         0x04

// Log levels (in PB_LOG)
#define PB_LOG_DEBUG        0x00
#define PB_LOG_INFO         0x01
#define PB_LOG_WARN         0x02
#define PB_LOG_ERROR        0x03

// NAK reasons (in PB_NAK)
#define PB_NAK_CRC          0x01
#define PB_NAK_LEN          0x02
#define PB_NAK_UNKNOWN_TYPE 0x03
#define PB_NAK_BUSY         0x04

// ─── Frame layout ────────────────────────────────────────────────────────────
// [0xAA] [0x55] [type] [seq] [len_hi] [len_lo] [payload...] [crc_hi] [crc_lo]
//                ↑─────── CRC16-CCITT covers from here ───────↑

#define PB_FRAME_OVERHEAD   8     // 2 magic + 1 type + 1 seq + 2 len + 2 crc
#define PB_MAX_PAYLOAD      1024  // per-frame cap; chunk PNGs above this
#define PB_MAX_FRAME        (PB_MAX_PAYLOAD + PB_FRAME_OVERHEAD)
