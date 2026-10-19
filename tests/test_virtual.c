// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/core/sg_virtual.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static SgVirtual v;
static unsigned mac_calls;
static void mac(const uint8_t *key, const uint8_t *rc, const uint8_t *blocks, uint8_t count,
                const uint8_t *data, uint8_t *out) {
    assert(key[0] == 0x55 && count == 2 && blocks[0] == 0x82 && blocks[1] == 0x91);
    assert(data[0] == 0x33);
    ++mac_calls;
    memset(out, rc[0], 8);
}
static bool poll(void *ctx, SgCard *card) { return sg_virtual_poll(ctx, card); }
static bool felica(void *ctx, const uint8_t *id, const uint8_t *req, size_t n, const uint8_t *rc,
                   size_t rn, uint8_t *out, size_t *on) {
    return sg_virtual_felica(ctx, id, req, n, rc, rn, out, on, mac);
}
static void initialize(void) {
    memset(&v, 0, sizeof(v));
    v.loaded = true;
    v.card.type = SgCardFelica;
    memset(v.card.id, 0x12, 8);
    v.card.pmm[1] = 0xf1;
    v.blocks[22][2] = 0x55;
    v.blocks[17][2] = 0x33;
    sg_virtual_present(&v, true);
}
static void check_rc_counter(void) {
    /* Sony Lite-S 1.4 section 3.1.13: RC is volatile and is not a WCNT event.
     * Saturation limits Write With MAC, not a simple RC write. */
    const uint32_t counters[] = {0, 0x123456, 0xfffe00, 0xffffff};
    uint8_t rc[32] = {32, 8, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12,
                      1, 9, 0, 1, 0x80, 0x80};
    uint8_t read[] = {16, 6, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12,
                      1, 11, 0, 1, 0x80, 0x90};
    uint8_t out[64];
    for(unsigned mode = 0; mode < 2; ++mode) {
        for(unsigned k = 0; k < sizeof(counters) / sizeof(counters[0]); ++k) {
            initialize();
            v.blocks[23][4] = mode ? 0xff : 0;
            uint8_t expected[16] = {(uint8_t)counters[k], (uint8_t)(counters[k] >> 8),
                                    (uint8_t)(counters[k] >> 16)};
            memcpy(v.blocks[24] + 2, expected, sizeof(expected));
            for(unsigned attempt = 0; attempt < 3; ++attempt) {
                size_t cap = sizeof(out);
                assert(sg_virtual_felica(&v, v.card.id, read, sizeof(read), NULL, 0, out, &cap, mac));
                assert(cap == 29 && !memcmp(out + 13, expected, 16));
                rc[16] = (uint8_t)(attempt + 1);
                cap = sizeof(out);
                assert(sg_virtual_felica(&v, v.card.id, rc, sizeof(rc), NULL, 0, out, &cap, mac));
                assert(cap == 12 && !out[10] && !out[11]);
                cap = sizeof(out);
                assert(sg_virtual_felica(&v, v.card.id, read, sizeof(read), rc, sizeof(rc), out, &cap, mac));
                assert(cap == 29 && !memcmp(out + 13, expected, 16));
                assert(!memcmp(v.blocks[24] + 2, expected, 16));
            }
            sg_virtual_present(&v, false);
            sg_virtual_present(&v, true);
            size_t cap = sizeof(out);
            assert(sg_virtual_felica(&v, v.card.id, read, sizeof(read), NULL, 0, out, &cap, mac));
            assert(cap == 29 && !memcmp(out + 13, expected, 16));
        }
    }
    initialize();
}
static size_t call(SgReader *r, const uint8_t *inner, size_t n, uint8_t *out) {
    uint8_t req[255] = {0};
    req[0] = (uint8_t)(13 + n);
    req[2] = 77;
    req[3] = 0x71;
    req[4] = (uint8_t)(8 + n);
    memcpy(req + 13, inner, n);
    return sg_reader_handle(r, req, 13 + n, out);
}
static void select_card(SgReader *r) {
    uint8_t p[] = {6, 0, 0xff, 0xff, 1, 0}, out[255];
    assert(call(r, p, sizeof(p), out) == 26 && out[4] == 0);
    assert(out[24] == 0x88 && out[25] == 0xb4);
}
static unsigned decoded;
static void on_frame(void *ctx, const uint8_t *bytes, size_t n) {
    (void)ctx;
    assert(n == 5 && bytes[2] == 0xe0);
    ++decoded;
}
int main(void) {
    check_rc_counter();
    initialize();
    /* Match complete system codes; mixed bytes must not select the card. */
    uint8_t polling[] = {6, 0, 0xff, 0xff, 1, 0};
    uint8_t polling_out[32];
    size_t polling_size = sizeof(polling_out);
    assert(sg_virtual_felica(&v, v.card.id, polling, sizeof(polling), NULL, 0, polling_out,
                             &polling_size, mac));
    polling[2] = 0x88;
    polling_size = sizeof(polling_out);
    assert(!sg_virtual_felica(&v, v.card.id, polling, sizeof(polling), NULL, 0, polling_out,
                              &polling_size, mac));
    polling[3] = 0xb4;
    polling_size = sizeof(polling_out);
    assert(sg_virtual_felica(&v, v.card.id, polling, sizeof(polling), NULL, 0, polling_out,
                             &polling_size, mac));
    polling[2] = 0xff;
    polling_size = sizeof(polling_out);
    assert(!sg_virtual_felica(&v, v.card.id, polling, sizeof(polling), NULL, 0, polling_out,
                              &polling_size, mac));
    SgReader reader;
    SgBackend backend = {.ctx = &v, .poll = poll, .felica = felica};
    sg_reader_init(&reader, SgProfileModern, backend);
    select_card(&reader);
    uint8_t read[] = {18,   6, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12,
                      0x12, 1, 11,   0,    2,    0x80, 0x82, 0x80, 0x91};
    uint8_t rc[32] = {32,   8,    0x12, 0x12, 0x12, 0x12, 0x12, 0x12,
                      0x12, 0x12, 1,    9,    0,    1,    0x80, 0x80};
    uint8_t out[255];
    assert(call(&reader, read, sizeof(read), out) == 18 && out[17] == 0xb2);
    rc[16] = 0x44;
    assert(call(&reader, rc, sizeof(rc), out) == 18 && out[4] == 0);
    assert(call(&reader, read, sizeof(read), out) == 51 && out[35] == 0x44);
    assert(mac_calls == 1);
    rc[16] = 0x66;
    call(&reader, rc, sizeof(rc), out);
    call(&reader, read, sizeof(read), out);
    assert(out[35] == 0x66 && mac_calls == 2);
    /* A mode/card change must retain a partially received USB frame and reader
     * configuration while invalidating identity and authentication. */
    SgDecoder decoder = {0};
    uint8_t frame[] = {5, 0, 0xe0, 0x30, 0}, wire[513];
    size_t wn = sg_frame_encode(frame, sizeof(frame), wire, sizeof(wire));
    sg_decoder_feed(&decoder, wire, 4, on_frame, NULL);
    reader.radio = false;
    sg_virtual_present(&v, false);
    sg_reader_clear_card(&reader);
    assert(!reader.felica_rc_len && !reader.radio && reader.backend.ctx == &v);
    sg_decoder_feed(&decoder, wire + 4, wn - 4, on_frame, NULL);
    assert(decoded == 1);
    assert(call(&reader, read, sizeof(read), out) == 6 && out[4] != 0);
    v.card.id[0] = 0x34; /* synthetic second file */
    sg_virtual_present(&v, true);
    select_card(&reader);
    assert(call(&reader, read, sizeof(read), out) == 6 && out[4] != 0);
    read[2] = 0x34;
    assert(call(&reader, read, sizeof(read), out) == 18 && out[17] == 0xb2);
    initialize();
    /* Bounds and malformed inputs are tested at the backend boundary too. */
    uint8_t p[255] = {0};
    for(unsigned size = 0; size <= 255; ++size) {
        p[0] = (uint8_t)size;
        for(unsigned cmd = 0; cmd < 256; ++cmd) {
            p[1] = (uint8_t)cmd;
            size_t cap = sizeof(out);
            sg_virtual_felica(&v, v.card.id, p, size, NULL, 0, out, &cap, mac);
        }
    }
    uint8_t one[] = {16, 6, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 1, 11, 0, 1, 0x80, 0};
    for(unsigned block = 0; block < 256; ++block) {
        one[15] = (uint8_t)block;
        size_t cap = sizeof(out);
        assert(sg_virtual_felica(&v, v.card.id, one, sizeof(one), NULL, 0, out, &cap, mac));
        assert(cap == 12 || cap == 29);
    }
    one[15] = 0x80;
    size_t cap = sizeof(out);
    assert(sg_virtual_felica(&v, v.card.id, one, sizeof(one), NULL, 0, out, &cap, mac));
    for(unsigned i = 13; i < 29; ++i)
        assert(out[i] == 0);
    memset(out, 0xab, sizeof(out));
    cap = 1;
    assert(!sg_virtual_felica(&v, v.card.id, one, sizeof(one), NULL, 0, out, &cap, mac));
    assert(out[0] == 0xab);
    /* Protected/missing blocks never become zero-filled successful reads. */
    one[15] = 0;
    v.blocks[23][8] = 1;
    cap = sizeof(out);
    assert(sg_virtual_felica(&v, v.card.id, one, sizeof(one), NULL, 0, out, &cap, mac));
    assert(cap == 12 && out[11] == 0xb1);
    v.blocks[23][8] = 0;
    v.blocks[0][0] = 1;
    cap = sizeof(out);
    assert(sg_virtual_felica(&v, v.card.id, one, sizeof(one), NULL, 0, out, &cap, mac));
    assert(out[11] == 0xb1);
    v.blocks[0][0] = 0;
    one[15] = 0x90;
    cap = sizeof(out);
    v.blocks[24][2] = 2;
    assert(sg_virtual_felica(&v, v.card.id, one, sizeof(one), NULL, 0, out, &cap, mac));
    assert(out[13] == 2);
    sg_virtual_present(&v, false);
    cap = sizeof(out);
    assert(!sg_virtual_felica(&v, v.card.id, one, sizeof(one), NULL, 0, out, &cap, mac));
    puts("Virtual card: RC preserves WCNT including saturation, challenge routing, identity isolation, continuity and bounds passed");
    return 0;
}
