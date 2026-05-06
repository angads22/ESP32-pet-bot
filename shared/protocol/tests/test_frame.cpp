// Host-side unit tests for shared/protocol/frame.cpp.
// Build & run: `make` in this directory.

#include "../frame.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, name)                                                      \
    do {                                                                       \
        if (cond) {                                                            \
            ++g_passed;                                                        \
            std::printf("  PASS %s\n", name);                                  \
        } else {                                                               \
            ++g_failed;                                                        \
            std::printf("  FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__);     \
        }                                                                      \
    } while (0)

// ─── Test: known-vector CRC ──────────────────────────────────────────────────
// CRC16-CCITT(0xFFFF) of "123456789" is 0x29B1 — standard reference vector.
static void test_crc_known_vector() {
    std::printf("[crc-known-vector]\n");
    const uint8_t data[] = {'1','2','3','4','5','6','7','8','9'};
    uint16_t crc = pb_crc16_ccitt(data, sizeof(data));
    CHECK(crc == 0x29B1, "CRC16-CCITT('123456789') == 0x29B1");
}

// ─── Test: round-trip ────────────────────────────────────────────────────────
static void test_round_trip_basic() {
    std::printf("[round-trip-basic]\n");
    uint8_t enc[PB_MAX_FRAME];
    uint8_t dec_buf[PB_MAX_PAYLOAD];
    pb_decoder_t d;
    pb_decoder_init(&d, dec_buf, sizeof(dec_buf));

    const uint8_t payload[] = {0x01, 0x02, 0x03, 0xFF, 0x80};
    size_t n = pb_encode(enc, sizeof(enc), PB_DRAW_TEXT, 42, payload, sizeof(payload));
    CHECK(n == sizeof(payload) + PB_FRAME_OVERHEAD, "encoded size matches");

    pb_frame_t out{};
    pb_status_t status = PB_NEED_MORE;
    for (size_t i = 0; i < n; ++i) {
        status = pb_feed(&d, enc[i], &out);
        if (i + 1 < n) {
            CHECK(status == PB_NEED_MORE, "intermediate byte returns NEED_MORE");
        }
    }
    CHECK(status == PB_OK, "final byte returns OK");
    CHECK(out.type == PB_DRAW_TEXT, "type round-tripped");
    CHECK(out.seq == 42, "seq round-tripped");
    CHECK(out.len == sizeof(payload), "len round-tripped");
    CHECK(memcmp(out.payload, payload, sizeof(payload)) == 0, "payload round-tripped");
}

// ─── Test: empty payload ─────────────────────────────────────────────────────
static void test_empty_payload() {
    std::printf("[empty-payload]\n");
    uint8_t enc[PB_MAX_FRAME];
    uint8_t dec_buf[16];
    pb_decoder_t d;
    pb_decoder_init(&d, dec_buf, sizeof(dec_buf));

    size_t n = pb_encode(enc, sizeof(enc), PB_CLEAR, 0, nullptr, 0);
    CHECK(n == PB_FRAME_OVERHEAD, "empty payload yields 8-byte frame");

    pb_frame_t out{};
    pb_status_t s = PB_NEED_MORE;
    for (size_t i = 0; i < n; ++i) s = pb_feed(&d, enc[i], &out);
    CHECK(s == PB_OK, "decoded OK");
    CHECK(out.type == PB_CLEAR, "type == CLEAR");
    CHECK(out.len == 0, "len == 0");
}

// ─── Test: random round-trip ─────────────────────────────────────────────────
static void test_random_round_trip(uint32_t seed, int iters) {
    std::printf("[random-round-trip seed=%u iters=%d]\n", seed, iters);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> len_dist(0, 600);
    std::uniform_int_distribution<int> byte_dist(0, 255);

    uint8_t enc[PB_MAX_FRAME];
    uint8_t dec_buf[PB_MAX_PAYLOAD];
    pb_decoder_t d;
    pb_decoder_init(&d, dec_buf, sizeof(dec_buf));

    int ok = 0;
    for (int it = 0; it < iters; ++it) {
        int len = len_dist(rng);
        std::vector<uint8_t> p(len);
        for (auto& b : p) b = (uint8_t)byte_dist(rng);
        uint8_t type = (uint8_t)byte_dist(rng);
        uint8_t seq  = (uint8_t)byte_dist(rng);

        size_t n = pb_encode(enc, sizeof(enc), type, seq, p.data(), (uint16_t)len);
        if (n == 0) continue;

        pb_frame_t out{};
        pb_status_t s = PB_NEED_MORE;
        for (size_t i = 0; i < n; ++i) s = pb_feed(&d, enc[i], &out);
        if (s == PB_OK
            && out.type == type
            && out.seq == seq
            && out.len == len
            && (len == 0 || memcmp(out.payload, p.data(), len) == 0)) {
            ++ok;
        }
    }
    CHECK(ok == iters, "all random frames round-tripped");
}

// ─── Test: corruption detected by CRC ────────────────────────────────────────
static void test_corruption_detected() {
    std::printf("[corruption-detected]\n");
    uint8_t enc[PB_MAX_FRAME];
    uint8_t dec_buf[PB_MAX_PAYLOAD];

    const uint8_t payload[] = {1, 2, 3, 4, 5, 6, 7, 8};
    size_t n = pb_encode(enc, sizeof(enc), PB_DRAW_RECT, 7, payload, sizeof(payload));

    // Flip one bit somewhere inside the CRC-covered region (not in magic).
    int detected = 0;
    int total    = 0;
    for (size_t flip_byte = 2; flip_byte < n; ++flip_byte) {
        for (int flip_bit = 0; flip_bit < 8; ++flip_bit) {
            ++total;
            std::vector<uint8_t> bad(enc, enc + n);
            bad[flip_byte] ^= (uint8_t)(1 << flip_bit);

            pb_decoder_t d;
            pb_decoder_init(&d, dec_buf, sizeof(dec_buf));
            pb_frame_t out{};
            bool got_ok = false;
            for (size_t i = 0; i < bad.size(); ++i) {
                pb_status_t s = pb_feed(&d, bad[i], &out);
                if (s == PB_OK) { got_ok = true; break; }
                if (s == PB_ERR_CRC || s == PB_ERR_LEN) break;
            }
            // A flip in the type/seq/len/payload/crc region should produce
            // EITHER PB_ERR_CRC, PB_ERR_LEN, OR no full-frame at all (decoder
            // re-syncs and waits). It must NOT produce a successful decode
            // matching the original payload.
            if (!got_ok) ++detected;
        }
    }
    CHECK(detected == total, "every single-bit flip in CRC-covered region rejected");
}

// ─── Test: partial feeding ───────────────────────────────────────────────────
static void test_partial_feed_one_byte_at_a_time_with_junk() {
    std::printf("[partial-feed-with-junk]\n");
    uint8_t enc[PB_MAX_FRAME];
    uint8_t dec_buf[PB_MAX_PAYLOAD];
    pb_decoder_t d;
    pb_decoder_init(&d, dec_buf, sizeof(dec_buf));

    const uint8_t payload[] = {'h','e','l','l','o'};
    size_t n = pb_encode(enc, sizeof(enc), PB_LOG, 99, payload, sizeof(payload));

    // Prepend junk that includes one false 0xAA.
    std::vector<uint8_t> stream = {0x00, 0xFF, 0xAA, 0x00, 0x33};
    stream.insert(stream.end(), enc, enc + n);

    pb_frame_t out{};
    pb_status_t s = PB_NEED_MORE;
    int ok_count = 0;
    for (uint8_t b : stream) {
        s = pb_feed(&d, b, &out);
        if (s == PB_OK) ++ok_count;
    }
    CHECK(ok_count == 1, "exactly one OK after junk + real frame");
    CHECK(out.type == PB_LOG, "type == LOG");
    CHECK(out.len == sizeof(payload), "len matches");
    CHECK(memcmp(out.payload, payload, sizeof(payload)) == 0, "payload matches");
}

// ─── Test: oversize length triggers PB_ERR_LEN ───────────────────────────────
static void test_oversize_len() {
    std::printf("[oversize-len]\n");
    uint8_t small_buf[16];
    pb_decoder_t d;
    pb_decoder_init(&d, small_buf, sizeof(small_buf));

    // Hand-craft a frame header that claims 200 bytes of payload — bigger
    // than the 16-byte buffer.
    uint8_t hdr[] = {PB_MAGIC0, PB_MAGIC1, PB_DRAW_TEXT, 0, 0x00, 0xC8};
    pb_frame_t out{};
    pb_status_t s = PB_NEED_MORE;
    for (auto b : hdr) s = pb_feed(&d, b, &out);
    CHECK(s == PB_ERR_LEN, "decoder returns PB_ERR_LEN when payload > buffer cap");
}

// ─── Test: back-to-back frames ───────────────────────────────────────────────
static void test_back_to_back_frames() {
    std::printf("[back-to-back-frames]\n");
    uint8_t a[PB_MAX_FRAME], b[PB_MAX_FRAME];
    uint8_t buf[PB_MAX_PAYLOAD];
    pb_decoder_t d;
    pb_decoder_init(&d, buf, sizeof(buf));

    const uint8_t pa[] = {1};
    const uint8_t pb_[] = {9, 9, 9};
    size_t na = pb_encode(a, sizeof(a), PB_HELLO, 1, pa, sizeof(pa));
    size_t nb = pb_encode(b, sizeof(b), PB_BTN_EVENT, 2, pb_, sizeof(pb_));

    std::vector<uint8_t> stream;
    stream.insert(stream.end(), a, a + na);
    stream.insert(stream.end(), b, b + nb);

    pb_frame_t out{};
    int ok = 0;
    uint8_t got_types[2]{0, 0};
    for (uint8_t bb : stream) {
        if (pb_feed(&d, bb, &out) == PB_OK) {
            if (ok < 2) got_types[ok] = out.type;
            ++ok;
        }
    }
    CHECK(ok == 2, "two frames decoded back-to-back");
    CHECK(got_types[0] == PB_HELLO, "first frame is HELLO");
    CHECK(got_types[1] == PB_BTN_EVENT, "second frame is BTN_EVENT");
}

// ─── Test: encoder rejects oversize ──────────────────────────────────────────
static void test_encoder_rejects_oversize() {
    std::printf("[encoder-rejects-oversize]\n");
    std::vector<uint8_t> p(PB_MAX_PAYLOAD + 1, 0xAA);
    uint8_t enc[PB_MAX_FRAME + 4];
    size_t n = pb_encode(enc, sizeof(enc), PB_DRAW_TEXT, 0, p.data(), (uint16_t)p.size());
    CHECK(n == 0, "encoder rejects payload > PB_MAX_PAYLOAD");
}

int main() {
    std::printf("PetBot frame.cpp host tests\n");
    test_crc_known_vector();
    test_round_trip_basic();
    test_empty_payload();
    test_random_round_trip(0xDEADBEEFu, 500);
    test_random_round_trip(0xC0FFEEu,   500);
    test_corruption_detected();
    test_partial_feed_one_byte_at_a_time_with_junk();
    test_oversize_len();
    test_back_to_back_frames();
    test_encoder_rejects_oversize();
    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
