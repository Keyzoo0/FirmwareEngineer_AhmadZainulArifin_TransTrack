/**
 * @file  boot_journal_f4.c
 * @brief Flash back-end of the boot journal (sector 4, 64 KB).
 */
#include "boot_journal_f4.h"
#include "flash_f4.h"
#include "flash_layout.h"

static int j_erase(void)
{
    if (flash_f4_unlock() != 0) {
        return -1;
    }
    int rc = flash_f4_erase_sector(JOURNAL_SECTOR);
    flash_f4_lock();
    return rc;
}

static int j_program(uint32_t offset, const void *data, uint32_t len)
{
    if (flash_f4_unlock() != 0) {
        return -1;
    }
    int rc = flash_f4_program(JOURNAL_ADDR + offset, data, len);
    flash_f4_lock();
    return rc;
}

static const journal_flash_t journal_flash = {
    .base    = (const uint8_t *)JOURNAL_ADDR,
    .size    = JOURNAL_SIZE,
    .erase   = j_erase,
    .program = j_program,
};

const journal_flash_t *boot_journal_f4(void)
{
    return &journal_flash;
}
