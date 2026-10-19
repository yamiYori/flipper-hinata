// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sg_reader.h"

/* Private in-memory image, never serialized by this application. */
typedef struct {
    SgCard card;
    uint8_t blocks[28][18]; /* SF1, SF2, data[16], Flipper Lite-S layout */
    bool loaded;
    bool present;
} SgVirtual;
typedef void (*SgVirtualMac)(const uint8_t key[16], const uint8_t rc[16], const uint8_t *blocks,
                             uint8_t count, const uint8_t *data, uint8_t out[16]);
void sg_virtual_present(SgVirtual *v, bool present);
bool sg_virtual_poll(SgVirtual *v, SgCard *card);
bool sg_virtual_felica(SgVirtual *v, const uint8_t id[8], const uint8_t *req, size_t size,
                       const uint8_t *rc, size_t rc_size, uint8_t *out, size_t *out_size,
                       SgVirtualMac mac);
