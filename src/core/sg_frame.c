// SPDX-License-Identifier: GPL-3.0-or-later
#include "sg_reader.h"

void sg_decoder_reset(SgDecoder *d) {
    d->used = 0;
    d->active = false;
    d->escaped = false;
}

void sg_decoder_feed(SgDecoder *d, const uint8_t *bytes, size_t size, SgFrameCallback callback,
                     void *ctx) {
    for(size_t i = 0; i < size; i++) {
        uint8_t b = bytes[i];
        if(b == 0xe0) {
            d->active = true;
            d->used = 0;
            d->escaped = false;
            continue;
        }
        if(!d->active)
            continue;
        if(d->escaped) {
            if(b != 0xcf && b != 0xdf) {
                d->rejected++;
                sg_decoder_reset(d);
                continue;
            }
            b++;
            d->escaped = false;
        } else if(b == 0xd0) {
            d->escaped = true;
            continue;
        }
        d->bytes[d->used++] = b;
        if(d->used == 1 && b < 5) {
            d->rejected++;
            sg_decoder_reset(d);
        } else if(d->used == (size_t)d->bytes[0] + 1) {
            uint8_t sum = 0;
            for(size_t j = 0; j + 1 < d->used; j++)
                sum += d->bytes[j];
            if(sum == b)
                callback(ctx, d->bytes, d->used - 1);
            else
                d->rejected++;
            sg_decoder_reset(d);
        }
    }
}

size_t sg_frame_encode(const uint8_t *frame, size_t size, uint8_t *out, size_t cap) {
    if(size < 1 || size > SG_FRAME_MAX || frame[0] != size)
        return 0;
    uint8_t sum = 0;
    size_t needed = 1;
    for(size_t i = 0; i < size; i++) {
        sum += frame[i];
        needed += (frame[i] == 0xd0 || frame[i] == 0xe0) ? 2 : 1;
    }
    needed += (sum == 0xd0 || sum == 0xe0) ? 2 : 1;
    if(cap < needed)
        return 0;
    size_t n = 0;
    out[n++] = 0xe0;
    for(size_t i = 0; i <= size; i++) {
        uint8_t b = i == size ? sum : frame[i];
        if(b == 0xd0 || b == 0xe0) {
            out[n++] = 0xd0;
            out[n++] = b - 1;
        } else
            out[n++] = b;
    }
    return n;
}
