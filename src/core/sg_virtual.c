// SPDX-License-Identifier: GPL-3.0-or-later
#include "sg_virtual.h"
#include <string.h>

static int index_of(unsigned b) {
    if(b <= 0x0e)
        return (int)b;
    if(b >= 0x80 && b <= 0x88)
        return (int)b - 0x80 + 15;
    if(b >= 0x90 && b <= 0x92)
        return (int)b - 0x90 + 24;
    return b == 0xa0 ? 27 : -1;
}
void sg_virtual_present(SgVirtual *v, bool present) {
    v->present = present && v->loaded;
}
bool sg_virtual_poll(SgVirtual *v, SgCard *card) {
    if(!v->loaded || !v->present)
        return false;
    *card = v->card;
    return true;
}
static bool sf(uint8_t *out, size_t *size, unsigned position, uint8_t error) {
    out[0] = 12;
    out[10] = (uint8_t)(1u << position);
    out[11] = error;
    *size = 12;
    return true;
}
bool sg_virtual_felica(SgVirtual *v, const uint8_t id[8], const uint8_t *p, size_t n,
                       const uint8_t *rc, size_t rc_n, uint8_t *out, size_t *out_n,
                       SgVirtualMac mac) {
    if(!v->loaded || !v->present || n < 2 ||
       (!sg_felica_read_request_valid(p, n) && !sg_felica_rc_request_valid(p, n)))
        return false;
    if(p[1] != 0 && (memcmp(id, v->card.id, 8) || memcmp(p + 2, id, 8)))
        return false;
    uint8_t result[77] = {0}; /* Lite-S: at most four blocks */
    size_t len = 0;
    result[1] = p[1] + 1;
    memcpy(result + 2, v->card.id, 8);
    bool has_rc = rc && sg_felica_rc_request_valid(rc, rc_n) && !memcmp(rc + 2, id, 8);
    switch(p[1]) {
    case 0:
        if(!((p[2] == 0xff && p[3] == 0xff) || (p[2] == 0x88 && p[3] == 0xb4)))
            return false;
        memcpy(result + 10, v->card.pmm, 8);
        len = p[4] ? 20 : 18;
        if(p[4] == 1) {
            result[18] = 0x88;
            result[19] = 0xb4;
        }
        /* Request code 2: Lite-S communication performance 0000. */
        break;
    case 2:
        len = 11u + 2u * p[10];
        result[10] = p[10];
        memset(result + 11, 0xff, len - 11); /* no service key metadata in v2 dump */
        break;
    case 4:
        len = 11;
        break;
    case 12:
        len = 13;
        result[10] = 1;
        result[11] = 0x88;
        result[12] = 0xb4;
        break;
    case 8:
        len = 12;
        /* The request validator only accepts volatile RC writes. These neither
         * increment WCNT nor inherit the Write With MAC saturation limit. */
        break;
    case 6: {
        size_t pos = 11u + 2u * p[10];
        uint8_t count = p[pos++];
        if(p[10] != 1 || count > 4) {
            sf(result, &len, 0, p[10] != 1 ? 0xa1 : 0xa2);
            result[10] = 0xff;
            break;
        }
        uint16_t service = p[11] | ((uint16_t)p[12] << 8);
        if(service != 9 && service != 11) {
            sf(result, &len, 0, 0xa6);
            break;
        }
        uint8_t numbers[4];
        size_t mac_start = 0;
        bool failed = false, old_mac = false, new_mac = false;
        unsigned mac_position = 0;
        for(unsigned i = 0; i < count; ++i) {
            uint8_t flag = p[pos++];
            unsigned block = p[pos++];
            if(!(flag & 0x80))
                block |= (unsigned)p[pos++] << 8;
            int ix = index_of(block);
            uint8_t error = 0;
            const uint8_t *mc = v->blocks[23] + 2;
            bool readonly =
                block <= 14
                    ? !((mc[0] | ((unsigned)mc[1] << 8)) & (1u << block))
                    : block == 0x81 || block == 0x90 || block == 0x83 || block == 0xa0 ||
                          ((block == 0x82 || (block >= 0x84 && block <= 0x87 && block != 0x85)) &&
                           mc[2] == 0);
            if(ix < 0 || (service == 9 && readonly))
                error = 0xa8;
            else if(block == 0x91 && !has_rc)
                error = 0xb2;
            else if(block <= 14 && ((mc[6] | ((unsigned)mc[7] << 8)) & (1u << block)))
                error = 0xb1;
            else if(block != 0x80 && block != 0x87 && block != 0x91 &&
                    (v->blocks[ix][0] || v->blocks[ix][1]))
                error = 0xb1;
            if(error) {
                sf(result, &len, i, error);
                failed = true;
                break;
            }
            numbers[i] = (uint8_t)block;
            uint8_t *data = result + 13 + 16 * i;
            if(block == 0x81)
                old_mac = true;
            if(block == 0x91) {
                if(!new_mac)
                    mac_position = i;
                new_mac = true;
            }
            if(old_mac && new_mac) {
                sf(result, &len, mac_position, 0xb0);
                failed = true;
                break;
            }
            if(block == 0x91) {
                if(i == 0 || i + 1u != count)
                    mac_start = i + 1;
                else {
                    if(!mac)
                        return false;
                    mac(v->blocks[22] + 2, rc + rc_n - 16, numbers + mac_start,
                        (uint8_t)(count - mac_start), result + 13 + 16 * mac_start, data);
                }
            } else if(block != 0x80 && block != 0x87) {
                memcpy(data, v->blocks[ix] + 2, 16);
                if(block == 0x92)
                    data[0] = 0; /* no external authentication */
            }
        }
        if(!failed) {
            len = 13u + 16u * count;
            result[10] = result[11] = 0;
            result[12] = count;
        }
        break;
    }
    default:
        return false;
    }
    if(*out_n < len)
        return false;
    result[0] = (uint8_t)len;
    memcpy(out, result, len);
    *out_n = len;
    return true;
}
