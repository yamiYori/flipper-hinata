// SPDX-License-Identifier: GPL-3.0-or-later
#include "sg_reader.h"
#include <string.h>

enum { Ok = 0, CardError = 1, NotAccepted = 2, InvalidCommand = 3, InvalidData = 4 };

static uint8_t sector(uint8_t block) { return block < 128 ? block / 4 : 32 + (block - 128) / 16; }

static void clear_rc(SgReader *r) {
    memset(r->felica_rc, 0, sizeof(r->felica_rc));
    r->felica_rc_len = 0;
}

void sg_reader_clear_card(SgReader *r) {
    clear_rc(r);
    memset(&r->card, 0, sizeof(r->card));
    memset(r->selected_felica_id, 0, sizeof(r->selected_felica_id));
    r->authenticated = false;
}

void sg_reader_init(SgReader *r, SgProfile profile, SgBackend backend) {
    memset(r, 0, sizeof(*r));
    r->backend = backend;
    r->profile = profile;
    r->radio = true;
}

static size_t reply(SgReader *r, const uint8_t *req, uint8_t *out, uint8_t status,
                    const uint8_t *data, size_t size) {
    out[0] = (uint8_t)(6 + size);
    out[1] = req[1];
    out[2] = req[2];
    out[3] = req[3];
    out[4] = status;
    out[5] = (uint8_t)size;
    if(size)
        memcpy(out + 6, data, size);
    r->last_status = status;
    if(status == CardError)
        r->card_errors++;
    return 6 + size;
}

/* Strict read-only FeliCa subset: Polling, Request Service, Request Response,
 * Read Without Encryption, Request System Code. Reject writes and unknown
 * commands before the RF backend sees them. Lengths include the FeliCa LEN byte. */
bool sg_felica_read_request_valid(const uint8_t *p, size_t n) {
    if(n < 2 || n > 242 || p[0] != n)
        return false;
    switch(p[1]) {
    case 0x00:
        return n == 6 && p[4] <= 2 &&
               (p[5] == 0 || p[5] == 1 || p[5] == 3 || p[5] == 7 || p[5] == 15);
    case 0x02:
        return n >= 13 && p[10] >= 1 && p[10] <= 32 && n == 11u + 2u * p[10];
    case 0x04:
    case 0x0c:
        return n == 10;
    case 0x06: {
        if(n < 16 || p[10] == 0 || p[10] > 16)
            return false;
        size_t pos = 11u + 2u * p[10];
        if(pos >= n)
            return false;
        uint8_t blocks = p[pos++];
        /* A SG response has at most 249 payload bytes: 13 + 16 * 14 = 237. */
        if(blocks == 0 || blocks > 14)
            return false;
        for(uint8_t i = 0; i < blocks; i++) {
            if(pos >= n || (p[pos] & 0x0f) >= p[10] || (p[pos] & 0x70))
                return false;
            pos += (p[pos] & 0x80) ? 2 : 3;
            if(pos > n)
                return false;
        }
        return pos == n;
    }
    default:
        return false;
    }
}

bool sg_felica_rc_request_valid(const uint8_t *p, size_t n) {
    if(n != 32 && n != 33)
        return false;
    if(p[0] != n || p[1] != 8 || p[10] != 1 || p[11] != 9 || p[12] != 0 || p[13] != 1)
        return false;
    return n == 32 ? p[14] == 0x80 && p[15] == 0x80 : p[14] == 0 && p[15] == 0x80 && p[16] == 0;
}

static bool uid_matches(SgReader *r, const uint8_t *uid) {
    return r->radio && r->card.type == SgCardMifare && memcmp(r->card.id, uid, 4) == 0;
}

size_t sg_reader_handle(SgReader *r, const uint8_t *req, size_t size, uint8_t out[SG_FRAME_MAX]) {
    if(size < 5 || size > SG_FRAME_MAX || req[0] != size)
        return 0;
    if(req[1] != 0 && req[1] != 8)
        return 0;
    r->requests++;
    r->last_command = req[3];
    uint8_t cmd = req[3];
    const uint8_t *p = req + 5;
    size_t n = req[4];
    uint8_t data[SG_PAYLOAD_MAX] = {0};
    size_t len = 0;
    uint8_t status = Ok;
    if(n != size - 5)
        return reply(r, req, out, InvalidData, NULL, 0);

    if(req[1] == 8) {
        switch(cmd) {
        case 0x81:
            /* Hardware never acknowledges this command, even malformed ones. */
            if(n == 3 && r->backend.led)
                r->backend.led(r->backend.ctx, p[0], p[1], p[2]);
            return 0;
        case 0xf5:
            if(n)
                status = InvalidData;
            else {
                len = r->profile == SgProfileModern ? 0 : 1;
                if(r->backend.led)
                    r->backend.led(r->backend.ctx, 0, 0, 0);
            }
            break;
        case 0xf0:
            if(n)
                status = InvalidData;
            else {
                static const uint8_t modern[] = {'0', '0', '0', '-',  '0',  '0',
                                                 '0', '0', '0', 0xff, 0x11, 0x40};
                static const uint8_t legacy[] = {'1', '5', '0', '8', '4', 0xff, 0x10, 0, 0x12};
                len = r->profile == SgProfileModern ? sizeof(modern) : sizeof(legacy);
                memcpy(data, r->profile == SgProfileModern ? modern : legacy, len);
            }
            break;
        default:
            status = InvalidCommand;
            break;
        }
        return reply(r, req, out, status, data, len);
    }

    switch(cmd) {
    case 0x30:
    case 0x32:
        if(n) {
            status = InvalidData;
            break;
        }
        if(r->profile == SgProfileModern) {
            if(cmd == 0x30) {
                data[0] = 0x94;
                len = 1;
            } else {
                memcpy(data, "837-15396", 9);
                len = 9;
            }
        } else {
            /* Match the 23-byte legacy Segatools reader identity, including NUL. */
            memcpy(data, cmd == 0x30 ? "TN32MSEC003S F/W Ver1.2E" : "TN32MSEC003S H/W Ver3.0J", 23);
            len = 23;
        }
        break;
    case 0x62:
        if(n) {
            status = InvalidData;
            break;
        }
        memset(r->keys, 0, sizeof(r->keys));
        memset(r->key_set, 0, sizeof(r->key_set));
        sg_reader_clear_card(r);
        r->radio = true;
        /* Observed hardware and Segatools return 03 for TO_NORMAL_MODE,
         * despite 03 also being the general unsupported-command status. */
        status = 3;
        break;
    case 0x40:
        if(n > 1 || (n == 1 && (p[0] & ~3u))) {
            status = InvalidData;
            break;
        }
        r->radio = true;
        sg_reader_clear_card(r);
        break;
    case 0x41:
        if(n) {
            status = InvalidData;
            break;
        }
        r->radio = false;
        sg_reader_clear_card(r);
        break;
    case 0x42: {
        if(n) {
            status = InvalidData;
            break;
        }
        clear_rc(r);
        SgCard card = {0};
        if(r->radio && r->backend.poll && r->backend.poll(r->backend.ctx, &card) &&
           (card.type == SgCardMifare || card.type == SgCardFelica)) {
            if(card.type != r->card.type || memcmp(card.id, r->card.id, sizeof(card.id)))
                r->authenticated = false;
            r->card = card;
            memcpy(r->selected_felica_id, card.id, 8);
            data[0] = 1;
            data[1] = card.type;
            data[2] = card.type == SgCardFelica ? 16 : 4;
            memcpy(data + 3, card.id, card.type == SgCardFelica ? 8 : 4);
            if(card.type == SgCardFelica)
                memcpy(data + 11, card.pmm, 8);
            len = 3 + data[2];
        } else {
            sg_reader_clear_card(r);
            len = 1;
        }
        break;
    }
    case 0x43:
        r->authenticated = false;
        if(n != 4)
            status = InvalidData;
        else if(!uid_matches(r, p))
            status = CardError;
        break;
    case 0x44:
        if(n != 0 && n != 4)
            status = InvalidData;
        else
            sg_reader_clear_card(r);
        break;
    case 0x50:
    case 0x54:
        if(n != 6) {
            status = InvalidData;
            break;
        }
        memcpy(r->keys[cmd == 0x54], p, 6);
        r->key_set[cmd == 0x54] = true;
        r->authenticated = false;
        break;
    case 0x51:
    case 0x55: {
        r->authenticated = false;
        bool b = cmd == 0x55;
        if(n != 5) {
            status = InvalidData;
            break;
        }
        if(!uid_matches(r, p) || !r->key_set[b] || !r->backend.mifare ||
           !r->backend.mifare(r->backend.ctx, p, p[4], r->keys[b], b, false, data)) {
            status = CardError;
        } else {
            r->authenticated = true;
            r->auth_key_b = b;
            r->auth_sector = sector(p[4]);
        }
        break;
    }
    case 0x52:
        if(n != 5) {
            status = InvalidData;
            break;
        }
        if(!uid_matches(r, p) || !r->authenticated || sector(p[4]) != r->auth_sector ||
           !r->backend.mifare ||
           !r->backend.mifare(r->backend.ctx, p, p[4], r->keys[r->auth_key_b], r->auth_key_b, true,
                              data)) {
            status = CardError;
            r->authenticated = false;
        } else
            len = 16;
        break;
    case 0x71: {
        bool rc_write = n >= 10 && sg_felica_rc_request_valid(p + 8, n - 8);
        if(n < 10 || (!sg_felica_read_request_valid(p + 8, n - 8) && !rc_write)) {
            clear_rc(r);
            status = InvalidData;
            break;
        }
        const uint8_t *inner = p + 8;
        if(inner[1] == 0 || rc_write)
            clear_rc(r);
        /* Reference readers forward the inner frame without enforcing the
         * outer eight-byte hint. Polling has no IDm and may discover a card
         * without a preceding SG 42. SG 41 stops discovery, not explicit
         * FeliCa transactions; the backend powers RF for each transaction.
         * Never use the hint as physical identity. */
        if(inner[1] == 0 && r->card.type != SgCardFelica) {
            SgCard card = {0};
            sg_reader_clear_card(r);
            if(r->backend.poll && r->backend.poll(r->backend.ctx, &card) &&
               card.type == SgCardFelica) {
                r->card = card;
                memcpy(r->selected_felica_id, card.id, 8);
            }
        }
        if(r->card.type != SgCardFelica ||
           (inner[1] != 0 && memcmp(inner + 2, r->selected_felica_id, 8))) {
            clear_rc(r);
            status = CardError;
            break;
        }
        /* A block number is not portable across FeliCa products. The physical
         * backend independently verifies Lite-S and system 88B4 before RC IO. */
        if(rc_write && r->card.pmm[1] != 0xf1) {
            status = NotAccepted;
            break;
        }
        len = sizeof(data);
        if(!r->backend.felica ||
           !r->backend.felica(r->backend.ctx, r->card.id, inner, n - 8,
                              inner[1] == 6 ? r->felica_rc : NULL,
                              inner[1] == 6 ? r->felica_rc_len : 0, data, &len) ||
           len < 10 || len > sizeof(data) || data[0] != len || data[1] != inner[1] + 1 ||
           (inner[1] != 0 && memcmp(data + 2, inner + 2, 8)) ||
           (inner[1] == 0 && len != (inner[4] == 0 ? 18u : 20u))) {
            status = CardError;
            len = 0;
            clear_rc(r);
        } else if(inner[1] == 0)
            memcpy(r->selected_felica_id, data + 2, 8);
        else if(rc_write) {
            if(len != 12) {
                status = CardError;
                len = 0;
            } else if(data[10] == 0 && data[11] == 0) {
                memcpy(r->felica_rc, inner, n - 8);
                r->felica_rc_len = n - 8;
            }
        } else if(inner[1] == 6 && (len < 12 || data[10] || data[11]))
            clear_rc(r);
        break;
    }
    /* Compatibility update handshake; data is discarded, never written to flash. */
    case 0x60:
        if(n)
            status = InvalidData;
        break;
    case 0x63:
        /* Compatibility handshake metadata is deliberately not interpreted. */
        break;
    case 0x61:
        status = r->profile == SgProfileModern ? 0x20 : NotAccepted;
        break;
    case 0x64:
        status = r->profile == SgProfileModern ? 0x08 : NotAccepted;
        break;
    case 0x53: /* Reader is intentionally read-only. */
    case 0x70:
        status = NotAccepted;
        break;
    default:
        status = InvalidCommand;
        break;
    }
    return reply(r, req, out, status, data, len);
}
