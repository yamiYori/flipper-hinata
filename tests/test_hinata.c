// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/core/hinata.h"
#include "../src/core/sg_virtual.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static SgVirtual card;
static unsigned calls, mac_calls, count;
static uint8_t output[4][64];
static bool ack_failure;
static bool classic_ok;
static bool classic(void *ctx, const uint8_t uid[4], uint8_t block, const uint8_t key[6],
                    bool key_b, bool read, uint8_t out[16]) {
    (void)ctx;
    calls++;
    if(!classic_ok || !card.present || memcmp(uid, card.card.id, 4) ||
       memcmp(key, "ABCDEF", 6) || !key_b || block > 3)
        return false;
    if(read)
        for(unsigned i = 0; i < 16; ++i) out[i] = (uint8_t)(0xa0 + i);
    return true;
}
static void mac(const uint8_t key[16], const uint8_t rc[16], const uint8_t *bs, uint8_t n,
                const uint8_t *data, uint8_t out[16]) {
    (void)key;
    (void)bs;
    (void)n;
    (void)data;
    mac_calls++;
    for(unsigned i = 0; i < 16; ++i)
        out[i] = rc[i] ^ 0xa5;
}
static bool poll(void *ctx, SgCard *c) {
    (void)ctx;
    calls++;
    return sg_virtual_poll(&card, c);
}
static bool felica(void *ctx, const uint8_t id[8], const uint8_t *p, size_t n, const uint8_t *rc,
                   size_t rc_n, uint8_t *out, size_t *out_n) {
    (void)ctx;
    calls++;
    return sg_virtual_felica(&card, id, p, n, rc, rc_n, out, out_n, mac);
}
static bool send(void *ctx, const uint8_t *p, size_t n) {
    (void)ctx;
    assert(n == 64 && count < 4);
    memcpy(output[count++], p, 64);
    return !ack_failure;
}
static void fixture(Hinata *h) {
    memset(&card, 0, sizeof(card));
    card.loaded = true;
    card.present = true;
    card.card.type = SgCardFelica;
    const uint8_t id[] = {1, 0x2e, 3, 4, 5, 6, 7, 8};
    memcpy(card.card.id, id, 8);
    card.card.pmm[1] = 0xf1;
    card.blocks[23][2] = 0xff;
    card.blocks[23][3] = 0xff;
    card.blocks[23][4] = 1;
    for(unsigned i = 0; i < 16; ++i)
        card.blocks[0][i + 2] = (uint8_t)i;
    hinata_init(h, (SgBackend){.poll = poll, .felica = felica, .mifare = classic});
    calls = mac_calls = count = 0;
    ack_failure = false;
    classic_ok = true;
}
static void pn(Hinata *h, uint8_t cmd, const uint8_t *p, size_t n) {
    uint8_t r[64] = {1, 0xe2, 0, 0, 0xff};
    assert(n <= 53);
    r[5] = n + 2;
    r[6] = (uint8_t)(-r[5]);
    r[7] = 0xd4;
    r[8] = cmd;
    if(n)
        memcpy(r + 9, p, n);
    uint8_t sum = 0;
    for(size_t i = 7; i < n + 9; ++i)
        sum += r[i];
    r[n + 9] = (uint8_t)(-sum);
    count = 0;
    hinata_handle(h, r, 64, send, NULL);
}
static void find(Hinata *h) {
    const uint8_t p[] = {1, 1, 0, 0xff, 0xff, 1, 0};
    pn(h, 0x4a, p, 7);
}
static const uint8_t *payload(uint8_t cmd) {
    assert(count == 2);
    assert(output[0][0] == 3 && output[0][1] == 0xe2 && output[0][5] == 0);
    const uint8_t *f = output[1] + 2;
    assert(f[0] == 0 && f[1] == 0 && f[2] == 0xff && f[5] == 0xd5 && f[6] == cmd + 1);
    assert((uint8_t)(f[3] + f[4]) == 0);
    uint8_t sum = 0;
    for(unsigned i = 5; i < 6u + f[3]; ++i)
        sum += f[i];
    assert(!sum);
    return f + 7;
}
static void legacy_tests(void) {
    Hinata h;
    fixture(&h);
    /* Exact request/response layout used by the inspected AimeIO DLL's
     * pre-2025040100 branch. No extra 0x0f wrapper in responses. */
    uint8_t a[64] = {1, 0x0f, 2, 0x10, 0};
    uint8_t f[64] = {1, 0x0f, 1, 0x14};
    hinata_handle(&h, a, 64, send, NULL);
    assert(count == 1 && output[0][0] == 3 && output[0][1] == 0x10 && output[0][2] == 0);
    h.sega.felica_rc_len = 32;
    count = 0;
    hinata_handle(&h, f, 64, send, NULL);
    assert(count == 1 && output[0][0] == 3 && output[0][1] == 0x14);
    assert(output[0][2] == 17 && output[0][3] == 0);
    assert(!memcmp(output[0] + 4, card.card.id, 8));
    assert(!memcmp(output[0] + 12, card.card.pmm, 8));
    assert(!h.sega.felica_rc_len && !h.pn_selected);
    card.present = false;
    count = 0;
    hinata_handle(&h, f, 4, send, NULL);
    assert(count == 1 && output[0][2] == 1 && output[0][3] != 0);
    for(unsigned i = 4; i < 64; ++i) assert(!output[0][i]);
    card.present = true;
    card.card.type = SgCardMifare;
    count = 0;
    hinata_handle(&h, a, 64, send, NULL);
    assert(count == 1 && output[0][2] == 4 && !memcmp(output[0] + 3, card.card.id, 4));
    count = 0;
    hinata_handle(&h, f, 64, send, NULL);
    assert(output[0][3] != 0);
    for(unsigned cmd = 0x11; cmd <= 0x12; ++cmd) {
        uint8_t req[] = {1, 0x0f, 1, (uint8_t)cmd};
        unsigned before = calls;
        count = 0;
        hinata_handle(&h, req, sizeof(req), send, NULL);
        assert(calls == before && count == 1 && output[0][1] == cmd);
        assert(output[0][2] == 1 && output[0][3] != 0);
    }
    const uint8_t malformed[][6] = {{1,0x0f,0}, {1,0x0f,62,0x14},
                                   {1,0x0f,2,0x14,0}, {1,0x0f,2,0x10,1}};
    for(unsigned i = 0; i < sizeof(malformed)/sizeof(malformed[0]); ++i) {
        unsigned before = calls;
        count = 0;
        hinata_handle(&h, malformed[i], sizeof(malformed[i]), send, NULL);
        assert(calls == before && count == 0 && h.last_status != 0);
    }
}
static void legacy_classic_tests(void) {
    Hinata h;
    fixture(&h);
    card.card.type = SgCardMifare;
    uint8_t discover[] = {1, 0x0f, 2, 0x10, 0};
    uint8_t auth[] = {1, 0x0f, 14, 0x11, 4, 2, 0x61, 'A', 'B', 'C', 'D', 'E', 'F', 1, 0x2e, 3, 4};
    uint8_t read[] = {1, 0x0f, 2, 0x12, 2};
    #define LEGACY(req) do { count = 0; hinata_handle(&h, req, sizeof(req), send, NULL); } while(0)
    LEGACY(read);
    assert(output[0][2] == 1 && output[0][3] == 1 && calls == 0);
    LEGACY(discover);
    LEGACY(auth);
    assert(count == 1 && output[0][1] == 0x11 && output[0][2] == 1 && output[0][3] == 0);
    assert(h.sega.authenticated && h.sega.auth_key_b);
    LEGACY(read);
    assert(output[0][1] == 0x12 && output[0][2] == 17 && output[0][3] == 0);
    for(unsigned i = 0; i < 16; ++i) assert(output[0][4 + i] == (uint8_t)(0xa0 + i));
    unsigned before = calls;
    read[4] = 4;
    LEGACY(read);
    assert(output[0][3] == 1 && calls == before && !h.sega.authenticated && !h.sega.key_set[1]);
    read[4] = 2;
    LEGACY(discover);
    auth[13] ^= 1;
    before = calls;
    LEGACY(auth);
    assert(calls == before && output[0][3] == 1);
    auth[13] ^= 1;
    for(unsigned cut = 0; cut < sizeof(auth); ++cut) {
        LEGACY(discover);
        before = calls;
        count = 0;hinata_handle(&h, auth, cut, send, NULL);
        assert(calls == before);
    }
    LEGACY(discover);
    LEGACY(auth);
    card.present = false;
    LEGACY(read);
    assert(output[0][3] == 1 && !h.sega.authenticated);
    for(unsigned i = 4; i < 64; ++i) assert(output[0][i] == 0);
    card.present = true;
    LEGACY(discover);
    LEGACY(auth);
    card.card.id[0] ^= 1;
    LEGACY(read);
    assert(output[0][3] == 1 && !h.sega.authenticated);
    card.card.id[0] ^= 1;
    LEGACY(discover);
    classic_ok = false;
    LEGACY(auth);
    assert(output[0][3] == 1 && !h.sega.key_set[1]);
    classic_ok = true;
    LEGACY(discover);
    LEGACY(auth);
    pn(&h, 2, NULL, 0);
    before = calls;
    LEGACY(read);
    assert(output[0][3] == 1 && calls == before);
    LEGACY(discover);
    ack_failure = true;
    LEGACY(auth);
    assert(!h.sega.authenticated && !h.sega.key_set[1]);
    ack_failure = false;
    #undef LEGACY
}
static void web_presence_tests(void) {
    Hinata h;
    fixture(&h);
    static const uint8_t profiles[][12] = {
        {0x0a,0x29,0x34,0x11,0x11,0x4d,0x85,0x61,0x6f,0x26,0x62,0x87},
        {0x0a,0x49,0xb4,0x0c,0x11,0x4d,0x85,0x61,0x6f,0x26,0x62,0x87},
        {0x0a,0x59,0xf4,0x3f,0x11,0x4d,0x85,0x61,0x6f,0x26,0x62,0x87},
        {0x0a,0x69,0xf4,0x2b,0x11,0x4d,0x85,0x61,0x6f,0x26,0x62,0x87},
        {0x0a,0x59,0xd4,0x30,0x11,0x4d,0x85,0x61,0x6f,0x26,0x62,0x87},
    };
    find(&h);
    assert(payload(0x4a)[0] == 1);
    card.present = false;
    find(&h);
    assert(payload(0x4a)[0] == 0);
    uint8_t target = 1;
    pn(&h, 0x52, &target, 1);
    for(unsigned i = 0; i < sizeof(profiles)/sizeof(profiles[0]); ++i) {
        unsigned before = calls;
        pn(&h, 0x32, profiles[i], 12);
        payload(0x32);
        assert(output[1][5] == 2 && calls == before);
        uint8_t type_a[] = {1,0};
        pn(&h, 0x4a, type_a, sizeof(type_a));
        assert(payload(0x4a)[0] == 0 && !h.pn_selected);
    }
    uint8_t unsupported[12];
    memcpy(unsupported, profiles[0], sizeof(unsupported));
    unsupported[4] ^= 1;
    unsigned before = calls;
    pn(&h, 0x32, unsupported, sizeof(unsupported));
    assert(output[1][7] == 0x7f && calls == before);
    pn(&h, 0x32, profiles[0], 11);
    assert(output[1][7] == 0x7f && calls == before);
}
static void tests(void) {
    legacy_tests();
    legacy_classic_tests();
    web_presence_tests();
    Hinata h;
    fixture(&h);
    uint8_t fw[] = {1, 1};
    hinata_handle(&h, fw, 2, send, NULL);
    assert(count == 1 && !memcmp(output[0] + 1, "2024090500", 10));
    pn(&h, 2, NULL, 0);
    const uint8_t *version = payload(2);
    assert(version[0] == 0x32 && version[1] == 1 && version[2] == 0 && version[3] == 5);
    find(&h);
    const uint8_t *p = payload(0x4a);
    assert(p[0] == 1 && p[1] == 1 && p[2] == 20 && !memcmp(p + 4, card.card.id, 8));
    assert(p[20] == 0x88 && p[21] == 0xb4);
    uint8_t read[17] = {1, 16, 6};
    memcpy(read + 3, card.card.id, 8);
    read[11] = 1;
    read[12] = 0x0b;
    read[14] = 1;
    read[15] = 0x80;
    read[16] = 0;
    pn(&h, 0x40, read, 17);
    p = payload(0x40);
    assert(p[0] == 0 && p[1] == 29 && p[2] == 7 && p[14] == 0 && p[29] == 15);
    uint8_t rc[33] = {1, 32, 8};
    memcpy(rc + 3, card.card.id, 8);
    rc[11] = 1;
    rc[12] = 9;
    rc[14] = 1;
    rc[15] = 0x80;
    rc[16] = 0x80;
    for(unsigned i = 0; i < 16; ++i)
        rc[17 + i] = i + 1;
    pn(&h, 0x40, rc, 33);
    p = payload(0x40);
    assert(p[0] == 0 && !p[11] && !p[12] && h.sega.felica_rc_len == 32);
    uint8_t macread[19];
    memcpy(macread, read, 17);
    macread[1] = 18;
    macread[14] = 2;
    macread[17] = 0x80;
    macread[18] = 0x91;
    pn(&h, 0x40, macread, 19);
    p = payload(0x40);
    assert(p[0] == 0 && mac_calls == 1 && p[30] == (1 ^ 0xa5));
    hinata_clear_session(&h);
    assert(h.sega.felica_rc_len == 0 && !h.pn_selected);
    pn(&h, 0x40, macread, 19);
    assert(payload(0x40)[0] == 0x2b && mac_calls == 1);
    find(&h);
    pn(&h, 0x40, macread, 19);
    p = payload(0x40);
    assert(p[12] == 0xb2 && mac_calls == 1);
    sg_virtual_present(&card, false);
    find(&h);
    assert(payload(0x4a)[0] == 0 && !h.pn_selected);
    sg_virtual_present(&card, true);
    find(&h);
    read[3] ^= 1;
    pn(&h, 0x40, read, 17);
    assert(payload(0x40)[0] != 0);
    read[3] ^= 1;
    unsigned before = calls;
    uint8_t wr[] = {1, 0xa0, 0};
    pn(&h, 0x40, wr, 3);
    assert(calls == before && payload(0x40)[0] != 0);
    pn(&h, 0x06, NULL, 0);
    assert(count == 2 && output[1][7] == 0x7f);
    before = calls;
    ack_failure = true;
    find(&h);
    assert(count == 1 && calls == before);
    ack_failure = false;
    uint8_t bad[] = {1, 0xe2, 0, 0, 0xff, 2, 0, 0xd4, 2, 0x2a, 0};
    count = 0;
    hinata_handle(&h, bad, sizeof(bad), send, NULL);
    assert(count == 1 && output[0][5] == 0xff);
    uint8_t e0[] = {1, 0xe0, 5, 0, 0x23, 0x42, 0};
    count = 0;
    hinata_handle(&h, e0, sizeof(e0), send, NULL);
    assert(count == 1 && output[0][4] == 0x23 && output[0][9] == 0x10 && output[0][10] == 16);
    uint8_t led_report[] = {1, 0xe0, 8, 8, 0x24, 0x81, 3, 0, 0, 0};
    count = 0;
    hinata_handle(&h, led_report, sizeof(led_report), send, NULL);
    assert(count == 0 && h.last_status == 0);
    uint8_t bad_e0[] = {1, 0xe0, 4, 0, 0, 0, 0};
    h.sega.felica_rc_len = 32;
    h.sega.key_set[0] = true;
    count = 0;
    hinata_handle(&h, bad_e0, sizeof(bad_e0), send, NULL);
    assert(count == 0 && h.last_status == 4 && !h.sega.felica_rc_len && !h.sega.key_set[0]);
    uint8_t update[] = {1, 0xf0};
    count = 0;
    hinata_handle(&h, update, 2, send, NULL);
    assert(count == 0);
    /* Exercise every request size with random malformed data and every PN
     * command with valid checksums under UBSan. Synthetic card data only. */
    uint32_t seed = 0x74657374;
    for(unsigned j = 0; j < 50000; ++j) {
        uint8_t r[64];
        for(unsigned i = 0; i < 64; ++i) {
            seed = seed * 1664525u + 1013904223u;
            r[i] = seed >> 24;
        }
        r[0] = 1;
        if(j % 2)
            r[1] = 0xe2;
        count = 0;
        hinata_handle(&h, r, j % 65, send, NULL);
    }
    for(unsigned cmd = 0; cmd < 256; cmd++)
        for(unsigned n = 0; n < 54; n++) {
            uint8_t zeros[53] = {0};
            pn(&h, cmd, zeros, n);
        }
    puts("HINATA core: handshake, E0, PN532, Lite-S RC/MAC lifecycle, malformed corpus PASS");
}
int main(int argc, char **argv) {
    if(argc == 1) {
        tests();
        return 0;
    }
    (void)argv;
    Hinata h;
    fixture(&h);
    char line[1024];
    /* Stateful local harness for the unmodified public Dart client. */
    while(fgets(line, sizeof(line), stdin)) {
        if(!strncmp(line, "classic", 7)) {
            card.card.type = SgCardMifare;
            sg_virtual_present(&card, true);
            hinata_clear_session(&h);
            puts("0");
        } else if(!strncmp(line, "remove", 6)) {
            sg_virtual_present(&card, false);
            hinata_clear_session(&h);
            puts("0");
        } else if(!strncmp(line, "present", 7)) {
            sg_virtual_present(&card, true);
            hinata_clear_session(&h);
            puts("0");
        } else {
            uint8_t r[64];
            size_t n = 0;
            char *p = line;
            unsigned x;
            while(n < 64 && sscanf(p, "%2x", &x) == 1) {
                r[n++] = (uint8_t)x;
                p += 2;
            }
            count = 0;
            hinata_handle(&h, r, n, send, NULL);
            printf("%u\n", count);
            for(unsigned i = 0; i < count; ++i) {
                for(unsigned k = 0; k < 64; ++k)
                    printf("%02x", output[i][k]);
                puts("");
            }
        }
        fflush(stdout);
    }
    return 0;
}
