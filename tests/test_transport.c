// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/usb_transport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct FuriMessageQueue {
    uint8_t data[8][sizeof(HinataRx)];
    unsigned count, size;
};
struct FuriSemaphore {
    unsigned value, max;
};
FuriMessageQueue *furi_message_queue_alloc(uint32_t n, uint32_t size) {
    assert(n == 8);
    FuriMessageQueue *q = calloc(1, sizeof(*q));
    q->size = size;
    return q;
}
void furi_message_queue_free(FuriMessageQueue *q) { free(q); }
FuriStatus furi_message_queue_put(FuriMessageQueue *q, const void *p, uint32_t t) {
    (void)t;
    if(q->count == 8)
        return FuriStatusErrorResource;
    memcpy(q->data[q->count++], p, q->size);
    return FuriStatusOk;
}
FuriStatus furi_message_queue_get(FuriMessageQueue *q, void *p, uint32_t t) {
    (void)t;
    if(!q->count)
        return FuriStatusErrorTimeout;
    memcpy(p, q->data[0], q->size);
    memmove(q->data, q->data[1], --q->count * q->size);
    return FuriStatusOk;
}
FuriSemaphore *furi_semaphore_alloc(uint32_t max, uint32_t initial) {
    FuriSemaphore *s = malloc(sizeof(*s));
    s->max = max;
    s->value = initial;
    return s;
}
void furi_semaphore_free(FuriSemaphore *s) { free(s); }
FuriStatus furi_semaphore_acquire(FuriSemaphore *s, uint32_t t) {
    (void)t;
    if(!s->value)
        return FuriStatusErrorTimeout;
    s->value--;
    return FuriStatusOk;
}
FuriStatus furi_semaphore_release(FuriSemaphore *s) {
    if(s->value == s->max)
        return FuriStatusErrorResource;
    s->value++;
    return FuriStatusOk;
}
static usbd_device dev;
static unsigned configured, writes, read_size;
static bool complete_tx = true;
static uint8_t rx[64], tx[64];
static bool ep_config(uint8_t ep, uint8_t type, uint16_t size) {
    assert((ep & 7) <= 3);
    assert(type == 2 || type == 3);
    assert(size == 64 || size == 8);
    configured++;
    return true;
}
static void ep_deconfig(uint8_t ep) { (void)ep; }
static int32_t ep_read(uint8_t ep, void *p, uint16_t n) {
    assert((ep & 7) <= 2);
    assert(n >= read_size);
    memcpy(p, rx, read_size);
    return read_size;
}
static int32_t ep_write(uint8_t ep, const void *p, uint16_t n) {
    assert(ep == 0x81 && n <= 64);
    memcpy(tx, p, n);
    writes++;
    if(complete_tx)
        dev.endpoint[1](&dev, usbd_evt_eptx, ep);
    return n;
}
static uint8_t connect(bool b) {
    (void)b;
    return 0;
}
static const struct usbd_driver driver = {.ep_config = ep_config,
                                          .ep_deconfig = ep_deconfig,
                                          .ep_read = ep_read,
                                          .ep_write = ep_write,
                                          .connect = connect};
int main(void) {
    HinataUsb u;
    hinata_usb_init_state(&u);
    dev.driver = &driver;
    hinata_usb_interface.init(&dev, &hinata_usb_interface, &u);
    assert(dev.config_callback && dev.control_callback);
    assert(dev.config_callback(&dev, 1) == usbd_ack && configured == 5 &&
           atomic_load(&u.connected));
    unsigned epoch = atomic_load(&u.epoch);
    rx[0] = 1;
    rx[1] = 1;
    read_size = 2;
    dev.endpoint[1](&dev, usbd_evt_eprx, 1);
    HinataRx report;
    assert(furi_message_queue_get(u.rx, &report, 0) == FuriStatusOk && report.size == 2 &&
           report.epoch == epoch);
    usbd_ctlreq *control = calloc(1, sizeof(usbd_ctlreq) + 64);
    *control = (usbd_ctlreq){
        .bmRequestType = 0x21, .bRequest = 9, .wValue = 0x0201, .wIndex = 2, .wLength = 2};
    control->data[0] = control->data[1] = 1;
    usbd_rqc_callback done = NULL;
    assert(dev.control_callback(&dev, control, &done) == usbd_ack && done);
    done(&dev, control);
    assert(furi_message_queue_get(u.rx, &report, 0) == FuriStatusOk && report.bytes[1] == 1);
    control->wLength = 65;
    done = NULL;
    assert(dev.control_callback(&dev, control, &done) == usbd_fail && !done);
    *control=(usbd_ctlreq){.bmRequestType=0xa1,.bRequest=1,.wValue=0x0103,.wIndex=2,.wLength=64};
    assert(dev.control_callback(&dev,control,&done)==usbd_ack);
    assert(dev.status.data_count==64 && ((uint8_t*)dev.status.data_ptr)[0]==3);
    control->wValue=0x0101;
    assert(dev.control_callback(&dev,control,&done)==usbd_fail);
    free(control);
    for(unsigned i = 0; i < 10; i++)
        dev.endpoint[1](&dev, usbd_evt_eprx, 1);
    assert(atomic_load(&u.dropped) == 2);
    uint8_t reply[64] = {3, 0xe2};
    assert(hinata_usb_send(&u, reply, 64, epoch));
    assert(writes == 1 && atomic_load(&u.sent) == 1);
    complete_tx = false;
    assert(!hinata_usb_send(&u, reply, 64, epoch));
    assert(atomic_load(&u.busy) && atomic_load(&u.timeouts) == 1);
    assert(!hinata_usb_send(&u, reply, 64, epoch));
    assert(writes == 2);
    dev.endpoint[1](&dev, usbd_evt_eptx, 0x81);
    assert(!atomic_load(&u.busy));
    complete_tx = true;
    assert(hinata_usb_send(&u, reply, 64, epoch));
    hinata_usb_interface.suspend(&dev);
    assert(!atomic_load(&u.connected) && atomic_load(&u.epoch) != epoch);
    assert(!hinata_usb_send(&u, reply, 64, epoch));
    dev.status.device_cfg = 1;
    hinata_usb_interface.wakeup(&dev);
    assert(atomic_load(&u.connected));
    assert(!hinata_usb_send(&u, reply, 64, epoch));
    assert(hinata_usb_send(&u, reply, 64, atomic_load(&u.epoch)));
    hinata_usb_interface.deinit(&dev);
    assert(!dev.control_callback && !dev.config_callback && !dev.endpoint[1]);
    hinata_usb_free_state(&u);
    puts("USB adapter mock: configuration, SET_REPORT, overflow, TX timeout/late completion, "
         "epochs and teardown PASS");
    return 0;
}
