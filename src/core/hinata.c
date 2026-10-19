// SPDX-License-Identifier: GPL-3.0-or-later
#include "hinata.h"
#include <string.h>

enum {
    PnTimeout = 1,
    PnNoBuffer = 7,
    PnInvalid = 0x10,
    PnAuth = 0x14,
    PnUnsupported = 0x12,
    PnNoCard = 0x2b
};
#define PN_SYNTAX_ERROR SIZE_MAX
static bool emit(HinataSend send, void *ctx, uint8_t cmd, const uint8_t *p, size_t n) {
    if(n > 62)
        return false;
    uint8_t out[64] = {3, cmd};
    if(n)
        memcpy(out + 2, p, n);
    return send(ctx, out, sizeof(out));
}
void hinata_clear_session(Hinata *h) {
    sg_reader_clear_card(&h->sega);
    memset(h->sega.keys, 0, sizeof(h->sega.keys));
    memset(h->sega.key_set, 0, sizeof(h->sega.key_set));
    h->pn_selected = false;
    h->legacy_selected = false;
}
void hinata_init(Hinata *h, SgBackend b) {
    memset(h, 0, sizeof(*h));
    sg_reader_init(&h->sega, SgProfileModern, b);
    h->config[0] = 255;
    h->config[1] = 0x82;
}
static size_t sega(Hinata *h, uint8_t cmd, const uint8_t *p, size_t n, uint8_t *out) {
    uint8_t req[SG_FRAME_MAX] = {0};
    if(n > SG_FRAME_MAX - 5)
        return 0;
    req[0] = n + 5;
    req[3] = cmd;
    req[4] = n;
    if(n)
        memcpy(req + 5, p, n);
    return sg_reader_handle(&h->sega, req, n + 5, out);
}
static size_t felica(Hinata *h, const uint8_t *p, size_t n, uint8_t *out) {
    if(n < 2 || n > 53 || p[0] != n) {
        out[0] = PnInvalid;
        return 1;
    }
    /* Single-report PN532 bridge: at most 2 blocks fit. Never truncate a
     * response and never perform an RC write if the reply cannot be returned. */
    if(p[1] == 6 && sg_felica_read_request_valid(p, n) && p[11 + 2 * p[10]] > 2) {
        out[0] = PnNoBuffer;
        return 1;
    }
    uint8_t payload[61], response[SG_FRAME_MAX] = {0};
    memcpy(payload, h->sega.selected_felica_id, 8);
    memcpy(payload + 8, p, n);
    size_t len = sega(h, 0x71, payload, n + 8, response);
    if(!len || response[4]) {
        out[0] = len && response[4] == 4 ? PnInvalid : PnTimeout;
        return 1;
    }
    size_t res_n = response[5];
    if(res_n > 52) {
        out[0] = PnNoBuffer;
        return 1;
    }
    out[0] = 0;
    memcpy(out + 1, response + 6, res_n);
    return res_n + 1;
}
static size_t pn_execute(Hinata *h, uint8_t cmd, const uint8_t *p, size_t n, uint8_t out[53]) {
    SgReader *r = &h->sega;
    out[0] = PnInvalid;
    switch(cmd) {
    case 0x02: /* PN532-compatible software facade, version 1.0, A/F subset */
        if(n)
            return PN_SYNTAX_ERROR;
        out[0] = 0x32;
        out[1] = 1;
        out[2] = 0;
        out[3] = 5;
        return 4;
    case 0x00: /* Diagnose communication line test only */
        if(n && p[0] == 0) {
            memcpy(out, p, n);
            return n;
        }
        return PN_SYNTAX_ERROR;
    case 0x14: /* Normal SAM mode has no secure-element implementation */
        if(n >= 1 && n <= 3 && p[0] == 1) {
            hinata_clear_session(h);
            return 0;
        }
        return PN_SYNTAX_ERROR;
    case 0x12:
        if(n == 1 && p[0] == 0)
            return 0;
        return PN_SYNTAX_ERROR;
    case 0x32:
        /* PN532 Type A profiles are portable host hints here: Flipper's own
         * NFC poller selects the ST25R3916 analog settings. Accept the known
         * HINATA Go profiles so an empty FeliCa -> Type A round can complete;
         * do not apply PN532 register bytes to a different chip. */
        if(n == 12 && p[0] == 0x0a) {
            static const uint8_t tail[] = {0x11, 0x4d, 0x85, 0x61, 0x6f, 0x26, 0x62, 0x87};
            static const uint8_t profiles[][3] = {
                {0x29, 0x34, 0x11}, {0x49, 0xb4, 0x0c}, {0x59, 0xf4, 0x3f},
                {0x69, 0xf4, 0x2b}, {0x59, 0xd4, 0x30},
            };
            if(!memcmp(p + 4, tail, sizeof(tail)))
                for(size_t i = 0; i < sizeof(profiles) / sizeof(profiles[0]); ++i)
                    if(!memcmp(p + 1, profiles[i], 3))
                        return 0;
        }
        if(n == 2 && p[0] == 1 && !(p[1] & ~3u)) {
            if(!(p[1] & 1))
                hinata_clear_session(h);
            return 0;
        }
        if(n == 4 && p[0] == 5 && p[1] == 0 && p[2] == 0 && p[3] == 0)
            return 0;
        h->unsupported++;
        return PN_SYNTAX_ERROR;
    case 0x4a: {
        hinata_clear_session(h);
        if(n < 2 || p[0] != 1 || (p[1] != 0 && p[1] != 1))
            return PN_SYNTAX_ERROR;
        if(p[1] == 0 && n != 2)
            return PN_SYNTAX_ERROR;
        if(p[1] == 1 && (n != 7 || p[2] != 0 || p[5] > 2 ||
                         !(p[6] == 0 || p[6] == 1 || p[6] == 3 || p[6] == 7 || p[6] == 15)))
            return PN_SYNTAX_ERROR;
        SgCard card = {0};
        SgCardType type = p[1] == 0 ? SgCardMifare : SgCardFelica;
        bool found = r->backend.poll_type
                         ? r->backend.poll_type(r->backend.ctx, type, &card)
                         : r->backend.poll && r->backend.poll(r->backend.ctx, &card);
        out[0] = 0;
        if(!found || card.type != type)
            return 1;
        r->card = card;
        memcpy(r->selected_felica_id, card.id, 8);
        r->radio = true;
        if(type == SgCardMifare) {
            out[0] = 1;
            out[1] = 1;
            out[2] = card.atqa[0];
            out[3] = card.atqa[1];
            out[4] = card.sak;
            out[5] = 4;
            memcpy(out + 6, card.id, 4);
            h->pn_selected = true;
            return 10;
        }
        uint8_t request[6] = {6, 0, p[3], p[4], p[5], p[6]}, response[53];
        size_t len = felica(h, request, 6, response);
        if(response[0] || len < 19) {
            hinata_clear_session(h);
            return 1;
        }
        out[0] = 1;
        out[1] = 1;
        memcpy(out + 2, response + 1, len - 1);
        h->pn_selected = true;
        return len + 1;
    }
    case 0x40:
        if(n < 2 || p[0] != 1)
            return 1;
        if(!h->pn_selected) {
            out[0] = PnNoCard;
            return 1;
        }
        if(r->card.type == SgCardFelica)
            return felica(h, p + 1, n - 1, out);
        if(r->card.type != SgCardMifare) {
            out[0] = PnNoCard;
            return 1;
        }
        if((p[1] == 0x60 || p[1] == 0x61) && n == 13) {
            uint8_t response[SG_FRAME_MAX], payload[5];
            if(memcmp(p + 9, r->card.id, 4)) {
                hinata_clear_session(h);
                out[0] = PnAuth;
                return 1;
            }
            sega(h, p[1] == 0x60 ? 0x50 : 0x54, p + 3, 6, response);
            memcpy(payload, p + 9, 4);
            payload[4] = p[2];
            sega(h, p[1] == 0x60 ? 0x51 : 0x55, payload, 5, response);
            out[0] = response[4] ? PnAuth : 0;
            return 1;
        }
        if(p[1] == 0x30 && n == 3) {
            uint8_t response[SG_FRAME_MAX], payload[5];
            memcpy(payload, r->card.id, 4);
            payload[4] = p[2];
            sega(h, 0x52, payload, 5, response);
            out[0] = response[4] ? PnAuth : 0;
            if(!out[0]) {
                memcpy(out + 1, response + 6, 16);
                return 17;
            }
            return 1;
        }
        h->unsupported++;
        out[0] = PnUnsupported;
        return 1;
    case 0x42:
        if(!h->pn_selected || r->card.type != SgCardFelica) {
            out[0] = PnNoCard;
            return 1;
        }
        return felica(h, p, n, out);
    case 0x44:
    case 0x52:
        if(n != 1 || p[0] > 1)
            return 1;
        hinata_clear_session(h);
        out[0] = 0;
        return 1;
    case 0x54:
        if(n != 1 || p[0] != 1)
            return 1;
        out[0] = h->pn_selected ? 0 : PnNoCard;
        return 1;
    default:
        h->unsupported++;
        return PN_SYNTAX_ERROR;
    }
    return 1;
}
static void pn532(Hinata *h, const uint8_t *p, size_t n, HinataSend send, void *ctx) {
    const uint8_t nack[] = {0, 0, 0xff, 0xff, 0, 0};
    if(n < 9 || p[0] || p[1] || p[2] != 0xff || p[3] < 2 || p[3] > 55 || (uint8_t)(p[3] + p[4]) ||
       (size_t)p[3] + 7 > n || p[5] != 0xd4 || p[p[3] + 6])
        goto bad;
    uint8_t sum = 0;
    for(size_t i = 5; i < 6u + p[3]; ++i)
        sum += p[i];
    if(sum)
        goto bad;
    const uint8_t ack[] = {0, 0, 0xff, 0, 0xff, 0};
    h->last_command = p[6];
    if(!emit(send, ctx, 0xe2, ack, sizeof(ack))) {
        hinata_clear_session(h);
        return;
    }
    uint8_t payload[53] = {0}, frame[62] = {0, 0, 0xff};
    size_t len = pn_execute(h, p[6], p + 7, p[3] - 2, payload);
    if(len == PN_SYNTAX_ERROR) {
        const uint8_t error[] = {0, 0, 0xff, 1, 0xff, 0x7f, 0x81, 0};
        h->last_status = 0x7f;
        hinata_clear_session(h);
        emit(send, ctx, 0xe2, error, sizeof(error));
        return;
    }
    h->last_status =
        (p[6] == 0x40 || p[6] == 0x42 || p[6] == 0x44 || p[6] == 0x52 || p[6] == 0x54) && len
            ? payload[0]
            : 0;
    if(h->last_status)
        hinata_clear_session(h);
    frame[3] = len + 2;
    frame[4] = (uint8_t)(-frame[3]);
    frame[5] = 0xd5;
    frame[6] = p[6] + 1;
    memcpy(frame + 7, payload, len);
    sum = 0;
    for(size_t i = 5; i < 7 + len; ++i)
        sum += frame[i];
    frame[7 + len] = (uint8_t)(-sum);
    if(!emit(send, ctx, 0xe2, frame, len + 9))
        hinata_clear_session(h);
    return;
bad:
    h->rejected++;
    h->last_status = PnInvalid;
    hinata_clear_session(h);
    emit(send, ctx, 0xe2, nack, sizeof(nack));
}
static void legacy_poll(Hinata *h, const uint8_t *p, size_t n, HinataSend send, void *ctx) {
    if(n < 2 || p[0] < 1 || (size_t)p[0] + 1 > n)
        goto bad;
    uint8_t cmd = p[1];
    h->last_command = cmd;
    if(cmd == 0x11 || cmd == 0x12) {
        /* Old AimeIO wire layout: auth = UIDLEN, block, 60/61, key[6], UID[4];
         * read = block. Replies start with length, status, optional block[16].
         * Reuse the checked backend; no keys or card contents are synthesized. */
        uint8_t response[18] = {1, 1}, sg[SG_FRAME_MAX] = {0}, request[5];
        size_t size = 2;
        bool ok = false;
        if(cmd == 0x11)
            h->sega.authenticated = false;
        if(!h->legacy_selected || h->sega.card.type != SgCardMifare)
            goto legacy_done;
        if(cmd == 0x11) {
            if(p[0] != 14 || p[2] != 4 || (p[4] != 0x60 && p[4] != 0x61) ||
               memcmp(p + 11, h->sega.card.id, 4))
                goto legacy_done;
            bool b = p[4] == 0x61;
            sega(h, b ? 0x54 : 0x50, p + 5, 6, sg);
            memcpy(request, p + 11, 4);
            request[4] = p[3];
            ok = sega(h, b ? 0x55 : 0x51, request, sizeof(request), sg) && !sg[4];
        } else {
            if(p[0] != 2)
                goto legacy_done;
            memcpy(request, h->sega.card.id, 4);
            request[4] = p[2];
            ok = sega(h, 0x52, request, sizeof(request), sg) && !sg[4] && sg[5] == 16;
            if(ok) {
                response[0] = 17;
                memcpy(response + 2, sg + 6, 16);
                size = sizeof(response);
            }
        }
legacy_done:
        if(ok)
            response[1] = 0;
        else
            hinata_clear_session(h);
        h->last_status = ok ? 0 : 1;
        if(!emit(send, ctx, cmd, response, size))
            hinata_clear_session(h);
        memset(sg, 0, sizeof(sg));
        return;
    }
    hinata_clear_session(h);
    if(!((cmd == 0x10 && p[0] == 2 && p[2] == 0) || (cmd == 0x14 && p[0] == 1)))
        goto bad;
    SgReader *r = &h->sega;
    SgCard card = {0};
    SgCardType type = cmd == 0x10 ? SgCardMifare : SgCardFelica;
    bool found = r->backend.poll_type
                     ? r->backend.poll_type(r->backend.ctx, type, &card)
                     : r->backend.poll && r->backend.poll(r->backend.ctx, &card);
    found = found && card.type == type;
    if(found && type == SgCardMifare) {
        r->card = card;
        r->radio = true;
        h->legacy_selected = true;
    }
    uint8_t response[18] = {0};
    size_t size = 1;
    if(cmd == 0x10) {
        if(found) {
            response[0] = 4;
            memcpy(response + 1, card.id, 4);
            size = 5;
        }
    } else {
        response[0] = found ? 17 : 1;
        response[1] = found ? 0 : 1;
        size = found ? 18 : 2;
        if(found) {
            memcpy(response + 2, card.id, 8);
            memcpy(response + 10, card.pmm, 8);
        }
    }
    if(!emit(send, ctx, cmd, response, size))
        hinata_clear_session(h);
    return;
bad:
    hinata_clear_session(h);
    h->rejected++;
    h->last_status = 4;
}
void hinata_handle(Hinata *h, const uint8_t *report, size_t n, HinataSend send, void *ctx) {
    if(n < 2 || n > 64 || report[0] != 1) {
        h->rejected++;
        return;
    }
    ++h->requests;
    uint8_t cmd = report[1];
    const uint8_t *p = report + 2;
    size_t len = n - 2;
    h->last_transport = cmd;
    h->last_command = cmd;
    h->last_status = 0;
    if(cmd == 1) {
        /* Compatibility level, NOT the Flipper app's version/build date. */
        uint8_t out[64] = {3};
        memcpy(out + 1, "2024090500", 10);
        send(ctx, out, 64);
    } else if(cmd == 7 && len >= 3) {
        if(h->sega.backend.led)
            h->sega.backend.led(h->sega.backend.ctx, p[0], p[1], p[2]);
    } else if(cmd == 0xea) {
        if(h->sega.backend.led)
            h->sega.backend.led(h->sega.backend.ctx, 0, 0, 0);
    } else if(cmd == 0xe8) {
        hinata_clear_session(h);
        h->sega.radio = true;
    } else if(cmd == 0x0f) {
        legacy_poll(h, p, len, send, ctx);
    } else if(cmd == 0xe2) {
        if(h->legacy_selected)
            hinata_clear_session(h);
        pn532(h, p, len, send, ctx);
    } else if(cmd == 0xe0) {
        if(h->legacy_selected)
            hinata_clear_session(h);
        h->pn_selected = false;
        uint8_t response[SG_FRAME_MAX] = {0};
        if(len < 5 || p[0] < 5 || p[0] > len) {
            h->rejected++;
            h->last_status = 4;
            hinata_clear_session(h);
            return;
        }
        h->last_command = p[3];
        /* USB E0 has its own bounded, unescaped frame. No CDC framing/checksum.
         * Reject firmware-update commands, including the old dummy handshake. */
        if(p[1] == 0 && p[3] >= 0x60 && p[3] <= 0x64) {
            memcpy(response, p, 4);
            response[0] = 6;
            response[4] = 2;
            response[5] = 0;
            h->unsupported++;
        } else {
            size_t rlen = sg_reader_handle(&h->sega, p, p[0], response);
            if(!rlen) {
                if(p[1] == 8 && p[3] == 0x81 && p[0] == 8 && p[4] == 3)
                    return;
                hinata_clear_session(h);
                h->last_status = 4;
                return;
            }
            if(rlen > 62) {
                response[0] = 6;
                response[4] = 4;
                response[5] = 0;
            }
            /* HINATA Go's E0 decoder: FeliCa=10h, Type A=20h. */
            if(p[3] == 0x42 && response[5] >= 3 && response[6] == 1)
                response[7] = response[7] == SgCardFelica ? 0x10 : 0x20;
        }
        h->last_status = response[4];
        if(h->last_status)
            hinata_clear_session(h);
        if(!emit(send, ctx, cmd, response, response[0]))
            hinata_clear_session(h);
    } else {
        /* No invented common error packet exists in the public HINATA wire
         * protocol. Unsupported commands have no reply and are diagnosed. */
        h->unsupported++;
        h->last_status = 0xff;
    }
}
