// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <furi.h>
#include <furi_hal_usb.h>
#include <stdatomic.h>

typedef struct {
    uint8_t bytes[64];
    uint8_t size;
    uint32_t epoch;
} HinataRx;
typedef struct {
    usbd_device *dev;
    FuriMessageQueue *rx;
    FuriSemaphore *tx_done;
    atomic_bool connected;
    atomic_bool busy;
    atomic_uint epoch, received, sent, dropped, timeouts, configurations;
    uint8_t idle;
    uint8_t line_coding[7];
    uint8_t last_report[64];
} HinataUsb;
extern FuriHalUsbInterface hinata_usb_interface;
void hinata_usb_init_state(HinataUsb *usb);
void hinata_usb_free_state(HinataUsb *usb);
bool hinata_usb_send(HinataUsb *usb, const uint8_t *report, size_t size, uint32_t epoch);
