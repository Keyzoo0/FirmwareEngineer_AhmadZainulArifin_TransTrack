/**
 * @file  fw_image.h
 * @brief Firmware image header written by scripts/prepare-firmware.py and checked by the
 *        bootloader before every jump (CRC-32 + SHA-256 + vector-table sanity check).
 *
 * Slot layout:  [ fw_image_header_t | 0xFF padding up to 512 B ][ vector table | code ... ]
 */
#ifndef FW_IMAGE_H
#define FW_IMAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "sha256.h"

#define FW_IMAGE_MAGIC          0x57465454UL   /* "TTFW" in little-endian byte order */
#define FW_IMAGE_HEADER_VERSION 1U

typedef struct __attribute__((packed)) {
    uint32_t magic;            /*   0 */
    uint16_t header_version;   /*   4 */
    uint8_t  target_slot;      /*   6  SLOT_A / SLOT_B (images are linked per slot) */
    uint8_t  reserved0;        /*   7 */
    uint8_t  ver_major;        /*   8 */
    uint8_t  ver_minor;        /*   9 */
    uint16_t ver_patch;        /*  10 */
    uint32_t image_size;       /*  12  bytes after the 512-byte header */
    uint32_t image_crc32;      /*  16 */
    uint32_t build_time;       /*  20  unix seconds */
    uint8_t  sha256[SHA256_DIGEST_SIZE]; /* 24 */
    char     git_rev[16];      /*  56 */
    uint8_t  reserved1[52];    /*  72 */
    uint32_t header_crc32;     /* 124  CRC-32 of bytes 0..123 */
} fw_image_header_t;

_Static_assert(sizeof(fw_image_header_t) == 128, "image header must stay 128 bytes");

typedef enum {
    FW_IMAGE_OK = 0,
    FW_IMAGE_ERR_MAGIC,
    FW_IMAGE_ERR_HEADER_CRC,
    FW_IMAGE_ERR_SLOT,
    FW_IMAGE_ERR_SIZE,
    FW_IMAGE_ERR_CRC,
    FW_IMAGE_ERR_SHA256,
    FW_IMAGE_ERR_VECTORS,
} fw_image_status_t;

/**
 * Verify an image that is mapped in memory.
 * @param slot_data   where the slot contents can be read (header at offset 0)
 * @param slot_addr   address the slot is linked/executed at (equal to slot_data on target)
 * @param slot_size   size of the slot in bytes
 * @param expected_slot SLOT_A / SLOT_B, the slot the image must have been linked for
 * @param ram_start/ram_end valid range for the initial stack pointer
 * @param check_sha   SHA-256 is slower (~60 ms for 384 KB @168 MHz); bootloader always enables it
 */
fw_image_status_t fw_image_verify(const uint8_t *slot_data, uint32_t slot_addr, uint32_t slot_size, uint8_t expected_slot,
                                  uint32_t ram_start, uint32_t ram_end, bool check_sha,
                                  fw_image_header_t *out_header);

const char *fw_image_status_str(fw_image_status_t st);

/** Version as a single comparable integer (major.minor.patch). */
static inline uint32_t fw_image_version(const fw_image_header_t *h)
{
    return ((uint32_t)h->ver_major << 24) | ((uint32_t)h->ver_minor << 16) | h->ver_patch;
}

#endif /* FW_IMAGE_H */
