/**
 * @file  boot_journal_f4.h
 * @brief Binds the boot journal to flash sector 4 on the STM32F4.
 */
#ifndef BOOT_JOURNAL_F4_H
#define BOOT_JOURNAL_F4_H

#include "boot_journal.h"

const journal_flash_t *boot_journal_f4(void);

#endif /* BOOT_JOURNAL_F4_H */
