/**
 * @file  crc32.h
 * @brief CRC-32/ISO-HDLC (same polynomial and reflection as zlib.crc32 / Python binascii.crc32)
 *        so the bootloader and scripts/prepare-firmware.py produce identical values.
 */
#ifndef CRC32_H
#define CRC32_H

#include <stddef.h>
#include <stdint.h>

/** Start a new CRC: call with crc = 0, then feed chunks; result is final (no extra XOR needed). */
uint32_t crc32_update(uint32_t crc, const void *data, size_t len);

static inline uint32_t crc32_compute(const void *data, size_t len)
{
    return crc32_update(0U, data, len);
}

#endif /* CRC32_H */
