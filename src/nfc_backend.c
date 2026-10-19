// SPDX-License-Identifier: GPL-3.0-or-later
#include "nfc_backend.h"
#include <furi.h>
#include <nfc/helpers/felica_crc.h>
#include <nfc/nfc.h>
#include <nfc/nfc_poller.h>
#include <nfc/protocols/felica/felica.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a_poller.h>
#include <nfc/protocols/mf_classic/mf_classic_poller.h>
#include <stdio.h>
#include <string.h>
#include <toolbox/bit_buffer.h>

/* The SG worker owns this object. NFC callbacks finish before the synchronous
 * operation returns. No USB callback is permitted to touch these fields. */
struct ReaderNfc {
    Nfc *nfc;
    NfcPoller *poller;
    FuriSemaphore *done;
    BitBuffer *tx;
    BitBuffer *rx;
    bool ok;
    SgCard card;
    uint8_t uid[4];
    MfClassicKey key;
    bool key_b;
    bool read;
    uint8_t block;
    uint8_t block_data[16];
    bool felica_detect;
    uint8_t expected_id[8];
    uint8_t felica_request[242];
    size_t felica_request_len;
    uint8_t felica_response[SG_PAYLOAD_MAX];
    size_t felica_response_len;
    uint16_t selected_system;
    uint8_t stage;
    uint8_t exchange_error;
    size_t received_size;
    uint8_t rc_request[33];
    size_t rc_request_len;
};

static void prepare(ReaderNfc *n) {
    while(furi_semaphore_acquire(n->done, 0) == FuriStatusOk) {
    }
    n->ok = false;
}

static NfcCommand type_a_callback(NfcGenericEvent event, void *ctx) {
    ReaderNfc *n = ctx;
    Iso14443_3aPollerEvent *e = event.event_data;
    if(e->type == Iso14443_3aPollerEventTypeReady) {
        const Iso14443_3aData *d = nfc_poller_get_data(n->poller);
        /* SEGA's classic-card ABI has exactly a four-byte UID. Never truncate
         * seven-byte NTAG/Ultralight/DESFire or map arbitrary tags to accounts. */
        if(d->uid_len == 4 && (d->sak & 0x08)) {
            memset(&n->card, 0, sizeof(n->card));
            n->card.type = SgCardMifare;
            memcpy(n->card.id, d->uid, 4);
            n->card.atqa[0] = d->atqa[1];
            n->card.atqa[1] = d->atqa[0];
            n->card.sak = d->sak;
            n->ok = true;
        }
    }
    furi_semaphore_release(n->done);
    return NfcCommandStop;
}

static NfcCommand mifare_callback(NfcGenericEvent event, void *ctx) {
    ReaderNfc *n = ctx;
    MfClassicPollerEvent *e = event.event_data;
    if(e->type == MfClassicPollerEventTypeRequestMode) {
        /* The stock poller processes this mode after our callback returns. */
        e->data->poller_mode.mode = MfClassicPollerModeRead;
        const MfClassicData *d = nfc_poller_get_data(n->poller);
        const Iso14443_3aData *a = d->iso14443_3a_data;
        if(a->uid_len == 4 && memcmp(a->uid, n->uid, 4) == 0 && (a->sak & 0x08)) {
            MfClassicPoller *p = event.instance;
            MfClassicError err = mf_classic_poller_auth(
                p, n->block, &n->key, n->key_b ? MfClassicKeyTypeB : MfClassicKeyTypeA, NULL,
                false);
            n->exchange_error = err;
            /* auth() refreshes the UID from the currently activated parent
             * poller. Detect-type may have reset the RF field before this event;
             * compare again to catch a card swap across that reset. */
            if(a->uid_len != 4 || memcmp(a->uid, n->uid, 4))
                err = MfClassicErrorNotPresent;
            if(err == MfClassicErrorNone && n->read) {
                n->stage = 12;
                MfClassicBlock block;
                err = mf_classic_poller_read_block(p, n->block, &block);
                if(err == MfClassicErrorNone) {
                    memcpy(n->block_data, block.data, 16);
                    n->received_size = 16;
                }
            }
            n->exchange_error = err;
            n->ok = err == MfClassicErrorNone;
            mf_classic_poller_halt(p);
        }
        furi_semaphore_release(n->done);
        return NfcCommandStop;
    }
    if(e->type == MfClassicPollerEventTypeCardLost || e->type == MfClassicPollerEventTypeFail) {
        furi_semaphore_release(n->done);
        return NfcCommandStop;
    }
    return NfcCommandContinue;
}

static bool felica_exchange(ReaderNfc *n, const uint8_t *req, size_t len) {
    n->exchange_error = 0;
    n->received_size = 0;
    bit_buffer_copy_bytes(n->tx, req, len);
    felica_crc_append(n->tx);
    bit_buffer_reset(n->rx);
    /* Carrier cycles, not milliseconds: about 22 ms. */
    NfcError error = nfc_poller_trx(n->nfc, n->tx, n->rx, 300000);
    n->received_size = bit_buffer_get_size_bytes(n->rx);
    if(error != NfcErrorNone) {
        n->exchange_error = error;
        return false;
    }
    if(!felica_crc_check(n->rx)) {
        n->exchange_error = 0x80;
        return false;
    }
    felica_crc_trim(n->rx);
    size_t size = bit_buffer_get_size_bytes(n->rx);
    const uint8_t *data = bit_buffer_get_data(n->rx);
    n->received_size = size;
    bool valid = size >= 2 && size <= SG_PAYLOAD_MAX && data[0] == size;
    if(!valid)
        n->exchange_error = 0x81;
    return valid;
}

static bool felica_poll_system(ReaderNfc *n, uint16_t system) {
    uint8_t poll[] = {6, 0, (uint8_t)(system >> 8), (uint8_t)system, 0, 0};
    return felica_exchange(n, poll, sizeof(poll)) && bit_buffer_get_size_bytes(n->rx) == 18 &&
           bit_buffer_get_byte(n->rx, 1) == 1;
}

static NfcCommand felica_callback(NfcEvent event, void *ctx) {
    ReaderNfc *n = ctx;
    if(event.type != NfcEventTypePollerReady)
        return NfcCommandContinue;
    do {
        n->stage = 1;
        if(!felica_poll_system(n, 0xffff))
            break;
        const uint8_t *bytes = bit_buffer_get_data(n->rx);
        if(n->felica_detect) {
            memset(&n->card, 0, sizeof(n->card));
            n->card.type = SgCardFelica;
            memcpy(n->card.id, bytes + 2, 8);
            memcpy(n->card.pmm, bytes + 10, 8);
            n->selected_system = 0xffff;
            n->ok = true;
            break;
        }
        /* Re-check the physical card in the SAME powered session as the read. */
        n->stage = 2;
        if(memcmp(bytes + 2, n->expected_id, 8))
            break;
        const uint8_t *req = n->felica_request;
        if(req[1] != 0) {
            n->stage = 3;
            if(n->selected_system != 0xffff && !felica_poll_system(n, n->selected_system))
                break;
            n->stage = 4;
            if(memcmp(bit_buffer_get_data(n->rx) + 2, req + 2, 8))
                break;
        }
        if(req[1] == 8 || n->rc_request_len) {
            /* Only permit the volatile challenge block on verified Lite-S.
             * Recheck on the actual powered card, not just cached metadata. */
            n->stage = 8;
            if(!felica_poll_system(n, 0x88b4))
                break;
            bytes = bit_buffer_get_data(n->rx);
            if(bytes[11] != 0xf1 || memcmp(bytes + 2, req + 2, 8))
                break;
            if(n->rc_request_len) {
                n->stage = 9;
                if(!felica_exchange(n, n->rc_request, n->rc_request_len))
                    break;
                bytes = bit_buffer_get_data(n->rx);
                if(bit_buffer_get_size_bytes(n->rx) != 12 || bytes[1] != 9 ||
                   memcmp(bytes + 2, req + 2, 8) || bytes[10] || bytes[11])
                    break;
            }
        }
        n->stage = 5;
        if(!felica_exchange(n, req, n->felica_request_len))
            break;
        n->stage = 6;
        bytes = bit_buffer_get_data(n->rx);
        size_t len = bit_buffer_get_size_bytes(n->rx);
        if(len < 10 || bytes[1] != req[1] + 1)
            break;
        if(req[1] != 0 && memcmp(bytes + 2, req + 2, 8))
            break;
        if(req[1] == 0) {
            if(len != (req[4] == 0 ? 18u : 20u))
                break;
            n->selected_system = ((uint16_t)req[2] << 8) | req[3];
        }
        memcpy(n->felica_response, bytes, len);
        n->felica_response_len = len;
        n->stage = 7;
        n->ok = true;
    } while(false);
    furi_semaphore_release(n->done);
    return NfcCommandStop;
}

static bool run_felica(ReaderNfc *n) {
    prepare(n);
    n->stage = 0;
    n->exchange_error = 0;
    n->received_size = 0;
    nfc_config(n->nfc, NfcModePoller, NfcTechFelica);
    nfc_set_guard_time_us(n->nfc, FELICA_GUARD_TIME_US);
    nfc_set_fdt_poll_fc(n->nfc, FELICA_FDT_POLL_FC);
    nfc_set_fdt_poll_poll_us(n->nfc, FELICA_POLL_POLL_MIN_US);
    nfc_set_mask_receive_time_fc(n->nfc, 0);
    nfc_start(n->nfc, felica_callback, n);
    bool completed = furi_semaphore_acquire(n->done, furi_ms_to_ticks(180)) == FuriStatusOk;
    nfc_stop(n->nfc);
    if(!completed)
        n->exchange_error = 0x82;
    return completed && n->ok;
}

void reader_nfc_diagnostic(ReaderNfc *n, char *out, size_t size) {
    snprintf(out, size, "RF:%u E:%02X N:%u", n->stage, n->exchange_error,
             (unsigned int)n->received_size);
}

static bool backend_poll_type(void *ctx, SgCardType type, SgCard *card) {
    ReaderNfc *n = ctx;
    n->felica_detect = true;
    if(type != SgCardMifare && run_felica(n)) {
        *card = n->card;
        return true;
    }
    if(type == SgCardFelica)
        return false;
    prepare(n);
    n->stage = 10;
    n->exchange_error = 0;
    n->received_size = 0;
    n->poller = nfc_poller_alloc(n->nfc, NfcProtocolIso14443_3a);
    nfc_poller_start(n->poller, type_a_callback, n);
    bool completed = furi_semaphore_acquire(n->done, furi_ms_to_ticks(150)) == FuriStatusOk;
    nfc_poller_stop(n->poller);
    nfc_poller_free(n->poller);
    n->poller = NULL;
    if(!completed)
        n->exchange_error = 0x82;
    if(completed && n->ok) {
        *card = n->card;
        return true;
    }
    return false;
}

static bool backend_poll(void *ctx, SgCard *card) {
    return backend_poll_type(ctx, SgCardNone, card);
}

static bool backend_mifare(void *ctx, const uint8_t uid[4], uint8_t block, const uint8_t key[6],
                           bool key_b, bool read, uint8_t output[16]) {
    ReaderNfc *n = ctx;
    prepare(n);
    n->stage = 11;
    n->exchange_error = 0;
    n->received_size = 0;
    memcpy(n->uid, uid, 4);
    memcpy(n->key.data, key, 6);
    n->key_b = key_b;
    n->read = read;
    n->block = block;
    n->poller = nfc_poller_alloc(n->nfc, NfcProtocolMfClassic);
    nfc_poller_start(n->poller, mifare_callback, n);
    bool completed = furi_semaphore_acquire(n->done, furi_ms_to_ticks(400)) == FuriStatusOk;
    nfc_poller_stop(n->poller);
    nfc_poller_free(n->poller);
    n->poller = NULL;
    if(!completed)
        n->exchange_error = 0x82;
    memset(&n->key, 0, sizeof(n->key));
    if(completed && n->ok && read)
        memcpy(output, n->block_data, 16);
    return completed && n->ok;
}

static bool backend_felica(void *ctx, const uint8_t id[8], const uint8_t *req, size_t size,
                           const uint8_t *rc, size_t rc_len, uint8_t *res, size_t *res_size) {
    ReaderNfc *n = ctx;
    if(!sg_felica_read_request_valid(req, size) && !sg_felica_rc_request_valid(req, size))
        return false;
    if(rc_len && (!rc || req[1] != 6 || !sg_felica_rc_request_valid(rc, rc_len) ||
                  memcmp(rc + 2, req + 2, 8)))
        return false;
    n->rc_request_len = rc_len;
    if(rc_len)
        memcpy(n->rc_request, rc, rc_len);
    memcpy(n->expected_id, id, 8);
    memcpy(n->felica_request, req, size);
    n->felica_request_len = size;
    n->felica_detect = false;
    bool ok = run_felica(n);
    memset(n->rc_request, 0, sizeof(n->rc_request));
    n->rc_request_len = 0;
    if(!ok || n->felica_response_len > *res_size)
        return false;
    memcpy(res, n->felica_response, n->felica_response_len);
    *res_size = n->felica_response_len;
    return true;
}

ReaderNfc *reader_nfc_alloc(void) {
    ReaderNfc *n = malloc(sizeof(ReaderNfc));
    memset(n, 0, sizeof(*n));
    n->nfc = nfc_alloc();
    n->done = furi_semaphore_alloc(1, 0);
    n->tx = bit_buffer_alloc(256);
    n->rx = bit_buffer_alloc(256);
    n->selected_system = 0xffff;
    return n;
}

void reader_nfc_free(ReaderNfc *n) {
    nfc_free(n->nfc);
    bit_buffer_free(n->tx);
    bit_buffer_free(n->rx);
    furi_semaphore_free(n->done);
    memset(n, 0, sizeof(*n));
    free(n);
}

SgBackend reader_nfc_backend(ReaderNfc *n) {
    return (SgBackend){.poll = backend_poll,
                       .mifare = backend_mifare,
                       .felica = backend_felica,
                       .ctx = n,
                       .poll_type = backend_poll_type};
}
