// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/hinata.h"
#include "nfc_backend.h"
#include "usb_transport.h"
#include "virtual_loader.h"
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char lines[6][32];
} Screen;
typedef struct {
    HinataUsb usb;
    Hinata protocol;
    ReaderNfc *nfc;
    SgBackend physical;
    SgVirtual card;
    VirtualLoader loader;
    FuriMessageQueue *keys;
    FuriMutex *screen_lock;
    Screen screen;
    bool virtual_mode, diagnostics, cardio, quit;
    char filename[32], message[32], rf[32];
    uint8_t last_cardio[8];
    uint32_t epoch, tx_epoch, last_host, last_cardio_poll, duration;
    uint8_t first_transport, first_command, first_status;
    bool first_error;
} App;
static bool poll_type(void *ctx, SgCardType type, SgCard *out) {
    App *a = ctx;
    if(a->virtual_mode)
        return type != SgCardMifare && sg_virtual_poll(&a->card, out);
    return a->physical.poll_type(a->physical.ctx, type, out);
}
static bool poll(void *ctx, SgCard *out) { return poll_type(ctx, SgCardNone, out); }
static bool mifare(void *ctx, const uint8_t uid[4], uint8_t block, const uint8_t key[6], bool key_b,
                   bool read, uint8_t out[16]) {
    App *a = ctx;
    return !a->virtual_mode &&
           a->physical.mifare(a->physical.ctx, uid, block, key, key_b, read, out);
}
static bool felica(void *ctx, const uint8_t id[8], const uint8_t *req, size_t n, const uint8_t *rc,
                   size_t rc_n, uint8_t *out, size_t *out_n) {
    App *a = ctx;
    return a->virtual_mode
               ? sg_virtual_felica(&a->card, id, req, n, rc, rc_n, out, out_n, virtual_mac)
               : a->physical.felica(a->physical.ctx, id, req, n, rc, rc_n, out, out_n);
}
static void led(void *ctx, uint8_t r, uint8_t g, uint8_t b) {
    UNUSED(ctx);
    furi_hal_light_set(LightRed, r);
    furi_hal_light_set(LightGreen, g);
    furi_hal_light_set(LightBlue, b);
}
static bool send(void *ctx, const uint8_t *data, size_t n) {
    App *a = ctx;
    return hinata_usb_send(&a->usb, data, n, a->tx_epoch);
}
static void draw(Canvas *canvas, void *ctx) {
    App *a = ctx;
    Screen s;
    furi_mutex_acquire(a->screen_lock, FuriWaitForever);
    s = a->screen;
    furi_mutex_release(a->screen_lock);
    canvas_set_font(canvas, FontSecondary);
    for(unsigned i = 0; i < 6; ++i)
        canvas_draw_str(canvas, 0, 9 + 10 * i, s.lines[i]);
}
static void input(InputEvent *e, void *ctx) {
    App *a = ctx;
    if(e->type == InputTypeShort || e->type == InputTypeLong)
        furi_message_queue_put(a->keys, e, 0);
}
static bool refresh(App *a) {
    Screen s = {0};
    if(a->diagnostics) {
        snprintf(s.lines[0], 32, "Diagnostics  USB:%s",
                 atomic_load(&a->usb.connected) ? "up" : "down");
        snprintf(s.lines[1], 32, "RX:%lu TX:%lu CFG:%lu",
                 (unsigned long)atomic_load(&a->usb.received),
                 (unsigned long)atomic_load(&a->usb.sent),
                 (unsigned long)atomic_load(&a->usb.configurations));
        snprintf(s.lines[2], 32, "T:%02X C:%02X S:%02X %lums", a->protocol.last_transport,
                 a->protocol.last_command, a->protocol.last_status, (unsigned long)a->duration);
        snprintf(s.lines[3], 32, "Drop:%lu TO:%lu %s", (unsigned long)atomic_load(&a->usb.dropped),
                 (unsigned long)atomic_load(&a->usb.timeouts), a->virtual_mode ? "Virt" : "Phys");
        snprintf(s.lines[4], 32, "First:%02X/%02X/%02X %s", a->first_transport, a->first_command,
                 a->first_status, a->first_error ? "error" : "none");
        if(a->virtual_mode && a->card.loaded)
            snprintf(s.lines[5], 32, "MAC key unverified");
        else
            snprintf(s.lines[5], 32, "%.20s  < clear", a->rf);
    } else {
        snprintf(s.lines[0], 32, "HINATA USB 1.0.0   %s",
                 atomic_load(&a->usb.connected) ? "UP" : "WAIT");
        snprintf(s.lines[1], 32, "%s  CardIO:%s",
                 a->virtual_mode ? "Virtual Lite-S" : "Physical NFC", a->cardio ? "on" : "off");
        snprintf(s.lines[2], 32, "%s",
                 a->virtual_mode ? (a->card.loaded ? a->filename : "No file selected")
                                 : "Place card on Flipper");
        snprintf(s.lines[3], 32, "%s",
                 a->message[0]
                     ? a->message
                     : (a->virtual_mode ? (a->card.present ? "Card PRESENT" : "Card REMOVED")
                                        : "Read-only / volatile RC"));
        snprintf(s.lines[4], 32, "Up mode  Down file  OK card");
        snprintf(s.lines[5], 32, "> Details  Hold OK CardIO");
    }
    furi_mutex_acquire(a->screen_lock, FuriWaitForever);
    bool changed = memcmp(&a->screen, &s, sizeof(s)) != 0;
    a->screen = s;
    furi_mutex_release(a->screen_lock);
    return changed;
}
static void clear_card(App *a) {
    hinata_clear_session(&a->protocol);
    sg_virtual_present(&a->card, false);
    a->message[0] = 0;
}
static void key(App *a, const InputEvent *e) {
    /* Ignore any parent-view input queued during the modal handoff. */
    if(a->loader.thread)
        return;
    if(e->type == InputTypeLong) {
        if(e->key == InputKeyOk && !a->diagnostics) {
            a->cardio = !a->cardio;
            hinata_clear_session(&a->protocol);
        }
        return;
    }
    switch(e->key) {
    case InputKeyBack:
        if(a->diagnostics)
            a->diagnostics = false;
        else
            a->quit = true;
        break;
    case InputKeyRight:
        a->diagnostics = !a->diagnostics;
        break;
    case InputKeyLeft:
        a->first_error = false;
        a->first_transport = a->first_command = a->first_status = 0;
        break;
    case InputKeyUp:
        if(!a->diagnostics) {
            clear_card(a);
            a->virtual_mode = !a->virtual_mode;
        }
        break;
    case InputKeyDown:
        if(!a->diagnostics && !a->loader.thread)
            virtual_loader_start(&a->loader);
        break;
    case InputKeyOk:
        if(!a->diagnostics && a->virtual_mode) {
            hinata_clear_session(&a->protocol);
            sg_virtual_present(&a->card, !a->card.present);
            a->message[0] = 0;
        }
        break;
    default:
        break;
    }
}
static void browser_result(App *a) {
    if(!a->loader.thread || !atomic_load(&a->loader.done))
        return;
    virtual_loader_join(&a->loader);
    if(a->loader.selected && !a->loader.error) {
        clear_card(a);
        a->card = a->loader.card;
        a->virtual_mode = true;
        snprintf(a->filename, sizeof(a->filename), "%s", a->loader.name);
        snprintf(a->message, sizeof(a->message), "Loaded; MAC unverified");
    } else if(a->loader.error)
        snprintf(a->message, sizeof(a->message), "%s", a->loader.error);
    memset(&a->loader.card, 0, sizeof(a->loader.card));
}
static void card_io(App *a, uint32_t now) {
    if(!a->cardio || now - a->last_host < furi_ms_to_ticks(1500) ||
       now - a->last_cardio_poll < furi_ms_to_ticks(300) || !atomic_load(&a->usb.connected))
        return;
    a->last_cardio_poll = now;
    SgCard card = {0};
    uint8_t report[9] = {2};
    if(poll_type(a, SgCardFelica, &card))
        memcpy(report + 1, card.id, 8);
    if(memcmp(report + 1, a->last_cardio, 8)) {
        hinata_clear_session(&a->protocol);
        a->tx_epoch = atomic_load(&a->usb.epoch);
        if(send(a, report, sizeof(report)))
            memcpy(a->last_cardio, report + 1, 8);
    }
}
int32_t hinata_usb_app(void *ctx) {
    UNUSED(ctx);
    App *a = calloc(1, sizeof(App));
    hinata_usb_init_state(&a->usb);
    a->keys = furi_message_queue_alloc(8, sizeof(InputEvent));
    a->screen_lock = furi_mutex_alloc(FuriMutexTypeNormal);
    a->nfc = reader_nfc_alloc();
    a->physical = reader_nfc_backend(a->nfc);
    hinata_init(&a->protocol, (SgBackend){.poll = poll,
                                          .mifare = mifare,
                                          .felica = felica,
                                          .led = led,
                                          .ctx = a,
                                          .poll_type = poll_type});
    FuriHalUsbInterface *previous = furi_hal_usb_get_config();
    bool started = furi_hal_usb_set_config(&hinata_usb_interface, &a->usb);
    if(!started)
        snprintf(a->message, sizeof(a->message), "USB busy; close other USB app");
    Gui *gui = furi_record_open(RECORD_GUI);
    ViewPort *vp = view_port_alloc();
    view_port_draw_callback_set(vp, draw, a);
    view_port_input_callback_set(vp, input, a);
    refresh(a);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);
    uint32_t last_ui = furi_get_tick();
    while(!a->quit) {
        uint32_t epoch = atomic_load(&a->usb.epoch);
        if(epoch != a->epoch) {
            a->epoch = epoch;
            hinata_clear_session(&a->protocol);
            memset(a->last_cardio, 0, 8);
        }
        HinataRx r;
        if(furi_message_queue_get(a->usb.rx, &r, 10) == FuriStatusOk && r.epoch == epoch) {
            uint32_t start = furi_get_tick();
            a->last_host = start;
            a->tx_epoch = epoch;
            hinata_handle(&a->protocol, r.bytes, r.size, send, a);
            a->duration = (furi_get_tick() - start) * 1000 / furi_kernel_get_tick_frequency();
            if(!a->virtual_mode)
                reader_nfc_diagnostic(a->nfc, a->rf, sizeof(a->rf));
            else
                snprintf(a->rf, sizeof(a->rf), "Lite-S RAM");
            if(!a->first_error && a->protocol.last_status) {
                a->first_error = true;
                a->first_transport = a->protocol.last_transport;
                a->first_command = a->protocol.last_command;
                a->first_status = a->protocol.last_status;
            }
        }
        InputEvent event;
        if(furi_message_queue_get(a->keys, &event, 0) == FuriStatusOk)
            key(a, &event);
        browser_result(a);
        if(!furi_message_queue_get_count(a->usb.rx))
            card_io(a, furi_get_tick());
        uint32_t now = furi_get_tick();
        if(!a->loader.thread && now - last_ui >= furi_ms_to_ticks(100)) {
            last_ui = now;
            if(refresh(a))
                view_port_update(vp);
        }
    }
    if(a->loader.thread)
        virtual_loader_join(&a->loader);
    if(started)
        furi_check(furi_hal_usb_set_config(previous, NULL));
    gui_remove_view_port(gui, vp);
    view_port_free(vp);
    furi_record_close(RECORD_GUI);
    led(a, 0, 0, 0);
    reader_nfc_free(a->nfc);
    furi_message_queue_free(a->keys);
    furi_mutex_free(a->screen_lock);
    hinata_usb_free_state(&a->usb);
    memset(a, 0, sizeof(*a));
    free(a);
    return started ? 0 : 1;
}
