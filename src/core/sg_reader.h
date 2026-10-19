// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SG_FRAME_MAX 255u
#define SG_WIRE_MAX 513u
#define SG_PAYLOAD_MAX 249u

typedef enum { SgCardNone = 0, SgCardMifare = 0x10, SgCardFelica = 0x20 } SgCardType;
typedef enum { SgProfileModern, SgProfileLegacy } SgProfile;
typedef struct {
    SgCardType type;
    uint8_t id[8];
    uint8_t pmm[8];
    uint8_t atqa[2]; /* PN532 order: high byte first */
    uint8_t sak;
} SgCard;

/* All callbacks run on the application worker, never on the USB interrupt thread.
 * Backend operations must check the expected physical card before doing any IO. */
typedef struct {
    bool (*poll)(void *ctx, SgCard *card);
    bool (*mifare)(void *ctx, const uint8_t uid[4], uint8_t block, const uint8_t key[6], bool key_b,
                   bool read, uint8_t output[16]);
    bool (*felica)(void *ctx, const uint8_t card_id[8], const uint8_t *req, size_t req_len,
                   const uint8_t *rc, size_t rc_len, uint8_t *res, size_t *res_len);
    void (*led)(void *ctx, uint8_t r, uint8_t g, uint8_t b);
    void *ctx;
    bool (*poll_type)(void *ctx, SgCardType type, SgCard *card);
} SgBackend;

typedef struct {
    uint8_t bytes[SG_FRAME_MAX + 1];
    size_t used;
    bool active;
    bool escaped;
    uint32_t rejected;
} SgDecoder;

typedef void (*SgFrameCallback)(void *ctx, const uint8_t *frame, size_t size);
void sg_decoder_reset(SgDecoder *decoder);
void sg_decoder_feed(SgDecoder *decoder, const uint8_t *bytes, size_t size,
                     SgFrameCallback callback, void *ctx);
size_t sg_frame_encode(const uint8_t *frame, size_t size, uint8_t *out, size_t capacity);
typedef bool (*SgUsbWrite)(void *ctx, const uint8_t *data, size_t size);
/* USB full-speed bulk endpoints require a short packet (possibly a ZLP) to
 * terminate an exact-multiple-of-64 response. The callback waits for TX done. */
bool sg_usb_send(const uint8_t *wire, size_t size, SgUsbWrite write, void *ctx);

typedef struct {
    SgBackend backend;
    SgProfile profile;
    bool radio; /* SG discovery enabled; does not gate explicit FeliCa IO. */
    SgCard card;
    uint8_t selected_felica_id[8];
    uint8_t felica_rc[33];
    size_t felica_rc_len;
    uint8_t keys[2][6];
    bool key_set[2];
    bool authenticated;
    bool auth_key_b;
    uint8_t auth_sector;
    uint32_t requests;
    uint32_t card_errors;
    uint8_t last_command;
    uint8_t last_status;
} SgReader;

void sg_reader_init(SgReader *reader, SgProfile profile, SgBackend backend);
void sg_reader_clear_card(SgReader *reader);
/* Returns an unframed response, or zero for another address / no-ACK LED command. */
size_t sg_reader_handle(SgReader *reader, const uint8_t *req, size_t size,
                        uint8_t out[SG_FRAME_MAX]);
bool sg_felica_read_request_valid(const uint8_t *req, size_t size);
/* Only Lite-S volatile RC block 80h, service 0009h, one block of 16 bytes. */
bool sg_felica_rc_request_valid(const uint8_t *req, size_t size);
