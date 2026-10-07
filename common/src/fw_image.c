/**
 * @file  fw_image.c
 * @brief Image header validation shared by bootloader, application (pre-commit check of a
 *        freshly written update) and the host unit tests.
 */
#include "fw_image.h"
#include "crc32.h"
#include "flash_layout.h"
#include <stddef.h>
#include <string.h>

fw_image_status_t fw_image_verify(const uint8_t *slot_data, uint32_t slot_addr, uint32_t slot_size, uint8_t expected_slot,
                                  uint32_t ram_start, uint32_t ram_end, bool check_sha,
                                  fw_image_header_t *out_header)
{
    fw_image_header_t hdr;
    memcpy(&hdr, slot_data, sizeof(hdr));
    if (out_header != NULL) {
        *out_header = hdr;
    }

    if (hdr.magic != FW_IMAGE_MAGIC || hdr.header_version != FW_IMAGE_HEADER_VERSION) {
        return FW_IMAGE_ERR_MAGIC;
    }
    if (crc32_compute(&hdr, offsetof(fw_image_header_t, header_crc32)) != hdr.header_crc32) {
        return FW_IMAGE_ERR_HEADER_CRC;
    }
    if (hdr.target_slot != expected_slot) {
        return FW_IMAGE_ERR_SLOT;
    }
    if (hdr.image_size < 8U || hdr.image_size > slot_size - IMAGE_HEADER_SIZE) {
        return FW_IMAGE_ERR_SIZE;
    }

    const uint8_t *image = slot_data + IMAGE_HEADER_SIZE;
    if (crc32_compute(image, hdr.image_size) != hdr.image_crc32) {
        return FW_IMAGE_ERR_CRC;
    }
    if (check_sha) {
        uint8_t digest[SHA256_DIGEST_SIZE];
        sha256_compute(image, hdr.image_size, digest);
        if (memcmp(digest, hdr.sha256, sizeof(digest)) != 0) {
            return FW_IMAGE_ERR_SHA256;
        }
    }

    /* Vector table sanity: initial MSP in RAM, reset handler inside this image, Thumb bit set */
    uint32_t sp, reset;
    memcpy(&sp, image, 4);
    memcpy(&reset, image + 4, 4);
    uint32_t img_start = slot_addr + IMAGE_HEADER_SIZE;   /* execution address, not read address */
    if (sp < ram_start || sp > ram_end || (sp & 0x3U) != 0U) {
        return FW_IMAGE_ERR_VECTORS;
    }
    if ((reset & 1U) == 0U || (reset & ~1U) < img_start || (reset & ~1U) >= img_start + hdr.image_size) {
        return FW_IMAGE_ERR_VECTORS;
    }
    return FW_IMAGE_OK;
}

const char *fw_image_status_str(fw_image_status_t st)
{
    switch (st) {
    case FW_IMAGE_OK:             return "ok";
    case FW_IMAGE_ERR_MAGIC:      return "bad magic";
    case FW_IMAGE_ERR_HEADER_CRC: return "bad header crc";
    case FW_IMAGE_ERR_SLOT:       return "wrong slot";
    case FW_IMAGE_ERR_SIZE:       return "bad size";
    case FW_IMAGE_ERR_CRC:        return "image crc mismatch";
    case FW_IMAGE_ERR_SHA256:     return "sha256 mismatch";
    case FW_IMAGE_ERR_VECTORS:    return "invalid vector table";
    default:                      return "unknown";
    }
}
