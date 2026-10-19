// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

/* One interface, three top-level collections. Windows pairing in hinata-rs
 * relies on collections sharing an instance parent. No keyboard/mouse usages.
 * Report IDs are unique across top-level collections: IN=3, OUT=1, CardIO=2.
 * This is a new descriptor derived from public client requirements, not an
 * asserted dump of genuine HINATA hardware. PID matches the HINATA Lite discovery filter for DLL interoperability.
 * Product strings still identify this experimental Flipper implementation. */
#define HINATA_VID 0xf822
#define HINATA_PID 0x0148
static const uint8_t hinata_report_descriptor[] = {
    0x05, 0x01, 0x09, 0x00, 0xa1, 0x01, 0x85, 0x03, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75,
    0x08, 0x95, 0x3f, 0x09, 0x00, 0x81, 0x02, 0xc0, 0x05, 0x06, 0x09, 0x00, 0xa1, 0x01,
    0x85, 0x01, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x3f, 0x09, 0x00, 0x91,
    0x02, 0xc0, 0x06, 0xca, 0xff, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x02, 0x09, 0x42, 0x15,
    0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x08, 0x81, 0x02, 0xc0,
};
/* CDC ACM is present for hinata-rs Windows discovery only. It is deliberately
 * not the old Aime USB Reader serial service or a firmware RPC channel. */
static const uint8_t hinata_config_descriptor[] = {
    9,    2,    107,  0,
    3,    1,    0,    0x80,
    250,  8,    11,   0,
    2,    2,    2,    1,
    0,    9,    4,    0,
    0,    1,    2,    2,
    1,    0,    5,    0x24,
    0,    0x10, 0x01, 5,
    0x24, 1,    0,    1,
    4,    0x24, 2,    2,
    5,    0x24, 6,    0,
    1,    7,    5,    0x83,
    3,    8,    0,    16,
    9,    4,    1,    0,
    2,    10,   0,    0,
    0,    7,    5,    0x02,
    2,    64,   0,    0,
    7,    5,    0x82, 2,
    64,   0,    0,    9,
    4,    2,    0,    2,
    3,    0,    0,    0,
    9,    0x21, 0x11, 0x01,
    0,    1,    0x22, sizeof(hinata_report_descriptor),
    0,    7,    5,    0x81,
    3,    64,   0,    1,
    7,    5,    0x01, 3,
    64,   0,    1,
};
