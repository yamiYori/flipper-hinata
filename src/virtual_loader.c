// SPDX-License-Identifier: GPL-3.0-or-later
#include "virtual_loader.h"
#include "card_browser.h"
#include <nfc/nfc_device.h>
#include <nfc/protocols/felica/felica.h>
#include <stdio.h>
#include <string.h>

void virtual_mac(const uint8_t key[16], const uint8_t rc[16], const uint8_t *blocks, uint8_t count,
                 const uint8_t *data, uint8_t out[16]) {
    /* These firmware exports set the DES schedule themselves. The context has
     * no heap pointers; zero initialization matches mbedtls_des3_init. */
    mbedtls_des3_context context = {0};
    uint8_t session[16];
    felica_calculate_session_key(&context, key, rc, session);
    felica_calculate_mac_read(&context, session, rc, blocks, count, data, out);
    volatile uint8_t *wipe = (volatile uint8_t *)&context;
    for(size_t i = 0; i < sizeof(context); ++i)
        wipe[i] = 0;
    wipe = session;
    for(size_t i = 0; i < sizeof(session); ++i)
        wipe[i] = 0;
}
static int32_t load_worker(void *ctx) {
    VirtualLoader *l = ctx;
    FuriString *path = furi_string_alloc_set("/ext/nfc");
    l->selected = card_browser_select(path);
    if(l->selected) {
        NfcDevice *device = nfc_device_alloc();
        if(!nfc_device_load(device, furi_string_get_cstr(path)))
            l->error = "Cannot load NFC file";
        else if(nfc_device_get_protocol(device) != NfcProtocolFelica)
            l->error = "Need FeliCa Lite-S file";
        else {
            const FelicaData *data = nfc_device_get_data(device, NfcProtocolFelica);
            if(data->pmm.data[1] != 0xf1 || data->blocks_total != 28 ||
               data->workflow_type != FelicaLite)
                l->error = "Need Lite-S v2 dump";
            else if(data->data.fs.mc.SF1 || data->data.fs.mc.SF2 || data->data.fs.id.SF1 ||
                    data->data.fs.id.SF2)
                l->error = "Missing ID / MC blocks";
            else {
                l->card.card.type = SgCardFelica;
                memcpy(l->card.card.id, data->idm.data, 8);
                memcpy(l->card.card.pmm, data->pmm.data, 8);
                memcpy(l->card.blocks, data->data.dump, sizeof(l->card.blocks));
                l->card.loaded = true;
                const char *name = strrchr(furi_string_get_cstr(path), '/');
                snprintf(l->name, sizeof(l->name), "%s", name ? name + 1 : "Saved card");
            }
        }
        nfc_device_free(device);
    }
    furi_string_free(path);
    atomic_store(&l->done, true);
    return 0;
}
void virtual_loader_start(VirtualLoader *l) {
    memset(&l->card, 0, sizeof(l->card));
    l->error = NULL;
    l->selected = false;
    atomic_store(&l->done, false);
    l->thread = furi_thread_alloc_ex("Card browser", 4096, load_worker, l);
    furi_thread_start(l->thread);
}
void virtual_loader_join(VirtualLoader *l) {
    furi_thread_join(l->thread);
    furi_thread_free(l->thread);
    l->thread = NULL;
}
