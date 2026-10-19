// SPDX-License-Identifier: GPL-3.0-or-later
#include "usb_transport.h"
#include "usb_descriptors.h"
#include <furi_hal.h>
#include <string.h>
#include <usb_hid.h>

static HinataUsb *active;
static struct usb_device_descriptor device_descriptor = {
    .bLength = 18,
    .bDescriptorType = 1,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0xef,
    .bDeviceSubClass = 2,
    .bDeviceProtocol = 1,
    .bMaxPacketSize0 = 8,
    .idVendor = HINATA_VID,
    .idProduct = HINATA_PID,
    .bcdDevice = 0x1000,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};
static uint16_t manufacturer[32], product[32], serial[32];
static void make_string(uint16_t *out, const char *s) {
    if(!s)
        s = "Flipper";
    size_t n = strlen(s);
    if(n > 31)
        n = 31;
    out[0] = (uint16_t)(0x0300 | (2 + 2 * n));
    for(size_t i = 0; i < n; ++i)
        out[i + 1] = (uint8_t)s[i];
}
static void receive(const uint8_t *bytes, size_t n) {
    if(!active || n < 2 || n > 64 || bytes[0] != 1)
        return;
    HinataRx report = {.size = (uint8_t)n, .epoch = atomic_load(&active->epoch)};
    memcpy(report.bytes, bytes, n);
    atomic_fetch_add(&active->received, 1);
    if(furi_message_queue_put(active->rx, &report, 0) != FuriStatusOk)
        atomic_fetch_add(&active->dropped, 1);
}
static void ep_callback(usbd_device *dev, uint8_t event, uint8_t ep) {
    if(!active)
        return;
    if(event == usbd_evt_eptx) {
        if((ep & 7) == 1) {
            atomic_store(&active->busy, false);
            furi_semaphore_release(active->tx_done);
        }
    } else if(event == usbd_evt_eprx) {
        uint8_t data[64];
        int32_t n = usbd_ep_read(dev, ep, data, sizeof(data));
        if((ep & 7) == 1 && n > 0)
            receive(data, (size_t)n);
        /* CDC data is discarded; no native serial reader is advertised. */
    }
}
static void epoch_end(void) {
    if(!active)
        return;
    atomic_store(&active->connected, false);
    atomic_fetch_add(&active->epoch, 1);
    furi_semaphore_release(active->tx_done);
}
static usbd_respond configure(usbd_device *dev, uint8_t cfg) {
    if(cfg > 1 || !active)
        return usbd_fail;
    epoch_end();
    for(uint8_t ep = 1; ep <= 3; ++ep) {
        usbd_ep_deconfig(dev, ep);
        usbd_reg_endpoint(dev, ep, NULL);
    }
    atomic_store(&active->busy, false);
    if(cfg == 0)
        return usbd_ack;
    bool ok = usbd_ep_config(dev, 0x81, USB_EPTYPE_INTERRUPT, 64);
    ok = usbd_ep_config(dev, 0x01, USB_EPTYPE_INTERRUPT, 64) && ok;
    ok = usbd_ep_config(dev, 0x82, USB_EPTYPE_BULK, 64) && ok;
    ok = usbd_ep_config(dev, 0x02, USB_EPTYPE_BULK, 64) && ok;
    ok = usbd_ep_config(dev, 0x83, USB_EPTYPE_INTERRUPT, 8) && ok;
    for(uint8_t ep = 1; ep <= 3; ++ep)
        usbd_reg_endpoint(dev, ep, ep_callback);
    atomic_store(&active->connected, ok);
    atomic_fetch_add(&active->configurations, 1);
    return ok ? usbd_ack : usbd_fail;
}
static void set_report_complete(usbd_device *dev, usbd_ctlreq *req) {
    UNUSED(dev);
    receive(req->data, req->wLength);
}
static usbd_respond control(usbd_device *dev, usbd_ctlreq *req, usbd_rqc_callback *complete) {
    if(!active)
        return usbd_fail;
    if(req->bmRequestType == 0x81 && req->bRequest == 6 && req->wIndex == 2) {
        if(req->wValue == 0x2200) {
            dev->status.data_ptr = (void *)hinata_report_descriptor;
            dev->status.data_count = sizeof(hinata_report_descriptor);
            return usbd_ack;
        }
        if(req->wValue == 0x2100) {
            dev->status.data_ptr = (void *)(hinata_config_descriptor + 84);
            dev->status.data_count = 9;
            return usbd_ack;
        }
    }
    if(req->wIndex == 2) {
        if(req->bmRequestType == 0x21 && req->bRequest == USB_HID_SETIDLE && !req->wLength) {
            active->idle = req->wValue >> 8;
            return usbd_ack;
        }
        if(req->bmRequestType == 0xa1 && req->bRequest == USB_HID_GETIDLE && req->wLength == 1) {
            dev->status.data_ptr = &active->idle;
            dev->status.data_count = 1;
            return usbd_ack;
        }
        if(req->bmRequestType == 0xa1 && req->bRequest == USB_HID_GETREPORT &&
           req->wValue == 0x0103) {
            dev->status.data_ptr = active->last_report;
            dev->status.data_count = 64;
            return usbd_ack;
        }
        if(req->bmRequestType == 0x21 && req->bRequest == USB_HID_SETREPORT &&
           req->wValue == 0x0201 && req->wLength >= 2 && req->wLength <= 64) {
            *complete = set_report_complete;
            return usbd_ack;
        }
    }
    if(req->wIndex == 0) {
        if(req->bmRequestType == 0x21 && req->bRequest == 0x20 && req->wLength == 7) {
            memcpy(active->line_coding, req->data, 7);
            return usbd_ack;
        }
        if(req->bmRequestType == 0xa1 && req->bRequest == 0x21 && req->wLength == 7) {
            dev->status.data_ptr = active->line_coding;
            dev->status.data_count = 7;
            return usbd_ack;
        }
        if(req->bmRequestType == 0x21 && (req->bRequest == 0x22 || req->bRequest == 0x23) &&
           !req->wLength)
            return usbd_ack;
    }
    return usbd_fail;
}
static void usb_init(usbd_device *dev, FuriHalUsbInterface *intf, void *ctx) {
    UNUSED(intf);
    active = ctx;
    active->dev = dev;
    usbd_reg_control(dev, control);
    usbd_reg_config(dev, configure);
    usbd_connect(dev, true);
}
static void usb_deinit(usbd_device *dev) {
    configure(dev, 0);
    usbd_reg_control(dev, NULL);
    usbd_reg_config(dev, NULL);
    active = NULL;
}
static void wakeup(usbd_device *dev) {
    if(active)
        atomic_store(&active->connected, dev->status.device_cfg == 1);
}
static void suspend(usbd_device *dev) {
    UNUSED(dev);
    epoch_end();
}
FuriHalUsbInterface hinata_usb_interface = {
    .init = usb_init,
    .deinit = usb_deinit,
    .wakeup = wakeup,
    .suspend = suspend,
    .dev_descr = &device_descriptor,
    .str_manuf_descr = manufacturer,
    .str_prod_descr = product,
    .str_serial_descr = serial,
    .cfg_descr = (void *)hinata_config_descriptor,
};
void hinata_usb_init_state(HinataUsb *u) {
    memset(u, 0, sizeof(*u));
    u->rx = furi_message_queue_alloc(8, sizeof(HinataRx));
    u->tx_done = furi_semaphore_alloc(1, 0);
    const uint8_t lc[] = {0x00, 0xc2, 0x01, 0, 0, 0, 8};
    memcpy(u->line_coding, lc, 7);
    u->last_report[0] = 3;
    make_string(manufacturer, "Flipper HINATA project");
    make_string(product, "HINATA USB experimental");
    make_string(serial, furi_hal_version_get_name_ptr());
}
void hinata_usb_free_state(HinataUsb *u) {
    furi_message_queue_free(u->rx);
    furi_semaphore_free(u->tx_done);
}
bool hinata_usb_send(HinataUsb *u, const uint8_t *data, size_t n, uint32_t epoch) {
    if(n > 64 || !n || !atomic_load(&u->connected) || atomic_load(&u->epoch) != epoch)
        return false;
    /* A timed-out transfer remains busy until its completion. Never overwrite
     * the PMA buffer or attribute a late completion to a newer response. */
    if(atomic_load(&u->busy))
        return false;
    while(furi_semaphore_acquire(u->tx_done, 0) == FuriStatusOk) {
    }
    bool wrote = false;
    FURI_CRITICAL_ENTER();
    if(atomic_load(&u->connected) && atomic_load(&u->epoch) == epoch) {
        atomic_store(&u->busy, true);
        wrote = usbd_ep_write(u->dev, 0x81, data, (uint16_t)n) == (int32_t)n;
        if(!wrote)
            atomic_store(&u->busy, false);
    }
    FURI_CRITICAL_EXIT();
    if(!wrote)
        return false;
    if(furi_semaphore_acquire(u->tx_done, furi_ms_to_ticks(250)) != FuriStatusOk) {
        atomic_fetch_add(&u->timeouts, 1);
        return false;
    }
    if(!atomic_load(&u->connected) || atomic_load(&u->epoch) != epoch)
        return false;
    atomic_fetch_add(&u->sent, 1);
    return true;
}
