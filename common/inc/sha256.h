/**
 * @file  sha256.h
 * @brief Minimal SHA-256 (FIPS 180-4) used by the bootloader for image integrity.
 */
#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_DIGEST_SIZE 32U

typedef struct {
    uint32_t state[8];
    uint64_t bit_len;
    uint8_t  block[64];
    uint32_t block_len;
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const void *data, size_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);
void sha256_compute(const void *data, size_t len, uint8_t digest[SHA256_DIGEST_SIZE]);

#endif /* SHA256_H */
