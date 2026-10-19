// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/sg_virtual.h"
#include <furi.h>
#include <stdatomic.h>

typedef struct {
    FuriThread *thread;
    atomic_bool done;
    SgVirtual card;
    char name[32];
    const char *error;
    bool selected;
} VirtualLoader;
void virtual_loader_start(VirtualLoader *loader);
void virtual_loader_join(VirtualLoader *loader);
void virtual_mac(const uint8_t key[16], const uint8_t rc[16], const uint8_t *blocks, uint8_t count,
                 const uint8_t *data, uint8_t out[16]);
