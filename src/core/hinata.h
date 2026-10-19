// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sg_reader.h"

typedef bool (*HinataSend)(void *ctx, const uint8_t *report, size_t size);
typedef struct {
    SgReader sega;
    bool pn_selected;
    bool legacy_selected;
    uint32_t requests, rejected, unsupported;
    uint8_t last_transport, last_command, last_status;
    uint8_t config[9];
} Hinata;
void hinata_init(Hinata *h, SgBackend backend);
void hinata_clear_session(Hinata *h);
void hinata_handle(Hinata *h, const uint8_t *report, size_t size, HinataSend send, void *ctx);
