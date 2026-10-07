/**
 * @file  update_proto.c
 * @brief Byte-wise frame decoder / encoder for the update protocol.
 */
#include "update_proto.h"
#include "crc32.h"
#include <string.h>

void up_decoder_init(up_decoder_t *d)
{
    memset(d, 0, sizeof(*d));
    d->st = UPS_SOF0;
}

static uint32_t frame_crc(const up_frame_t *f)
{
    uint8_t hdr[4] = { f->type, f->seq, (uint8_t)(f->len & 0xFFU), (uint8_t)(f->len >> 8) };
    uint32_t c = crc32_update(0U, hdr, sizeof(hdr));
    return crc32_update(c, f->payload, f->len);
}

up_dec_result_t up_decoder_feed(up_decoder_t *d, uint8_t b)
{
    switch (d->st) {
    case UPS_SOF0:
        if (b == UP_SOF0) d->st = UPS_SOF1;
        break;
    case UPS_SOF1:
        d->st = (b == UP_SOF1) ? UPS_HDR : (b == UP_SOF0 ? UPS_SOF1 : UPS_SOF0);
        d->idx = 0;
        break;
    case UPS_HDR:
        d->hdr[d->idx++] = b;
        if (d->idx == 4U) {
            d->frame.type = d->hdr[0];
            d->frame.seq  = d->hdr[1];
            d->frame.len  = (uint16_t)(d->hdr[2] | (d->hdr[3] << 8));
            d->idx = 0;
            if (d->frame.len > UP_MAX_PAYLOAD) {
                d->st = UPS_SOF0;              /* impossible length: resync */
            } else {
                d->st = (d->frame.len == 0U) ? UPS_CRC : UPS_PAYLOAD;
            }
        }
        break;
    case UPS_PAYLOAD:
        d->frame.payload[d->idx++] = b;
        if (d->idx == d->frame.len) {
            d->idx = 0;
            d->st = UPS_CRC;
        }
        break;
    case UPS_CRC:
        d->crc[d->idx++] = b;
        if (d->idx == 4U) {
            uint32_t rx = (uint32_t)d->crc[0] | ((uint32_t)d->crc[1] << 8) |
                          ((uint32_t)d->crc[2] << 16) | ((uint32_t)d->crc[3] << 24);
            d->st = UPS_SOF0;
            if (rx == frame_crc(&d->frame)) {
                return UP_DEC_FRAME;
            }
            d->crc_errors++;
            return UP_DEC_CRC_ERROR;
        }
        break;
    default:
        d->st = UPS_SOF0;
        break;
    }
    return UP_DEC_NONE;
}

uint16_t up_encode(uint8_t type, uint8_t seq, const void *payload, uint16_t len, uint8_t *out)
{
    out[0] = UP_SOF0;
    out[1] = UP_SOF1;
    out[2] = type;
    out[3] = seq;
    out[4] = (uint8_t)(len & 0xFFU);
    out[5] = (uint8_t)(len >> 8);
    if (len > 0U) memcpy(&out[6], payload, len);
    uint32_t c = crc32_update(0U, &out[2], 4U + len);
    out[6 + len] = (uint8_t)c;
    out[7 + len] = (uint8_t)(c >> 8);
    out[8 + len] = (uint8_t)(c >> 16);
    out[9 + len] = (uint8_t)(c >> 24);
    return (uint16_t)(len + UP_OVERHEAD);
}
