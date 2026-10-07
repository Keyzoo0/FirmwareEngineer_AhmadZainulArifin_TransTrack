/**
 * @file  update_proto.h
 * @brief Framed, CRC-protected firmware-update protocol on USART1 (host-testable).
 *
 * Frame:  0xA5 0x5A | type:u8 | seq:u8 | len:u16 LE | payload[len] | crc32:u32 LE
 *         CRC-32 covers type..payload. Max payload 1024 bytes.
 *
 * Host -> device               Device -> host
 *   START  (fw header, 128 B)    ACK  (status:u8, arg:u32)   status 0 = ok
 *   DATA   (offset:u32, bytes)   NAK  (status:u8, arg:u32)
 *   END    (-)
 *   ABORT  (-)
 *   INFO   (-)                   INFO (running slot, version, boot state)
 *
 * Stop-and-wait: the host sends the next frame only after the ACK for the previous one,
 * so a 2 KB receive buffer can never overflow and every frame is acknowledged.
 */
#ifndef UPDATE_PROTO_H
#define UPDATE_PROTO_H

#include <stdbool.h>
#include <stdint.h>

#define UP_SOF0             0xA5U
#define UP_SOF1             0x5AU
#define UP_MAX_PAYLOAD      1024U
#define UP_OVERHEAD         10U   /* 2 SOF + type + seq + 2 len + 4 crc */

typedef enum {
    UP_START = 0x01,
    UP_DATA  = 0x02,
    UP_END   = 0x03,
    UP_ABORT = 0x04,
    UP_INFO  = 0x05,
    UP_ACK   = 0x80,
    UP_NAK   = 0x81,
} up_type_t;

typedef enum {
    UP_OK = 0,
    UP_E_STATE,         /* frame not valid in the current session state */
    UP_E_HEADER,        /* START: bad magic / header CRC */
    UP_E_SLOT,          /* START: image linked for the running slot */
    UP_E_SIZE,
    UP_E_OFFSET,        /* DATA: not the next expected offset */
    UP_E_FLASH,
    UP_E_VERIFY,        /* END: CRC-32 / SHA-256 of the written image failed */
    UP_E_CRC,           /* frame CRC wrong */
} up_status_t;

typedef struct {
    uint8_t  type;
    uint8_t  seq;
    uint16_t len;
    uint8_t  payload[UP_MAX_PAYLOAD];
} up_frame_t;

typedef struct {
    enum { UPS_SOF0, UPS_SOF1, UPS_HDR, UPS_PAYLOAD, UPS_CRC } st;
    uint8_t  hdr[4];
    uint16_t idx;
    uint8_t  crc[4];
    up_frame_t frame;
    uint32_t crc_errors;
} up_decoder_t;

typedef enum { UP_DEC_NONE = 0, UP_DEC_FRAME, UP_DEC_CRC_ERROR } up_dec_result_t;

void            up_decoder_init(up_decoder_t *d);
up_dec_result_t up_decoder_feed(up_decoder_t *d, uint8_t b);

/** Encode a frame into out (size >= len + UP_OVERHEAD). Returns total length. */
uint16_t up_encode(uint8_t type, uint8_t seq, const void *payload, uint16_t len, uint8_t *out);

#endif /* UPDATE_PROTO_H */
