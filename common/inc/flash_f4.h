/**
 * @file  flash_f4.h
 * @brief Minimal register-level STM32F4 flash driver shared by bootloader and app.
 *
 * Kept HAL-free so the bootloader stays small and both images program flash the
 * same way. All functions assume VDD is 2.7-3.6 V (x32 parallelism).
 *
 * NOTE: the F407 has a single flash bank. While a sector is being erased or
 * programmed, any instruction fetch from flash stalls the CPU (up to ~2 s for a
 * 128 KB sector). Callers must keep the watchdog timeout above that.
 */
#ifndef FLASH_F4_H
#define FLASH_F4_H

#include <stdint.h>

int  flash_f4_unlock(void);
void flash_f4_lock(void);
int  flash_f4_erase_sector(uint32_t sector);
/** Program len bytes at a word-aligned address. A trailing partial word is padded with 0xFF. */
int  flash_f4_program(uint32_t addr, const void *data, uint32_t len);

#endif /* FLASH_F4_H */
