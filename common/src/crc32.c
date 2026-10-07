/**
 * @file  crc32.c
 * @brief Nibble-table CRC-32 (64-byte table, ~4x faster than bitwise, fits in a 32 KB bootloader).
 */
#include "crc32.h"

static const uint32_t crc_nibble_table[16] = {
    0x00000000U, 0x1DB71064U, 0x3B6E20C8U, 0x26D930ACU,
    0x76DC4190U, 0x6B6B51F4U, 0x4DB26158U, 0x5005713CU,
    0xEDB88320U, 0xF00F9344U, 0xD6D6A3E8U, 0xCB61B38CU,
    0x9B64C2B0U, 0x86D3D2D4U, 0xA00AE278U, 0xBDBDF21CU,
};

uint32_t crc32_update(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;

    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ crc_nibble_table[crc & 0x0FU];
        crc = (crc >> 4) ^ crc_nibble_table[crc & 0x0FU];
    }
    return ~crc;
}
