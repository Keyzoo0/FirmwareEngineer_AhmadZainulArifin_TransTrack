/**
 * @file  flash_f4.c
 * @brief Register-level STM32F4 flash erase/program (RM0090 section 3.6).
 */
#include "flash_f4.h"
#include "stm32f4xx.h"
#include <string.h>

#define F4_FLASH_KEY1          0x45670123UL
#define F4_FLASH_KEY2          0xCDEF89ABUL
#define FLASH_ERR_FLAGS     (FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | \
                             FLASH_SR_PGPERR | FLASH_SR_PGSERR)
#define FLASH_BUSY_LOOPS    50000000UL   /* > 4 s at 168 MHz; worst-case 128K erase is 2 s */

static int wait_ready(void)
{
    uint32_t n = FLASH_BUSY_LOOPS;
    while ((FLASH->SR & FLASH_SR_BSY) != 0U) {
        if (--n == 0U) {
            return -1;
        }
    }
    if ((FLASH->SR & FLASH_ERR_FLAGS) != 0U) {
        FLASH->SR = FLASH_ERR_FLAGS;    /* write-1-to-clear */
        return -2;
    }
    return 0;
}

/* After an erase the ART data cache may still hold the old contents. */
static void flush_caches(void)
{
    uint32_t acr = FLASH->ACR;
    FLASH->ACR = acr & ~(FLASH_ACR_DCEN | FLASH_ACR_ICEN);
    FLASH->ACR |= FLASH_ACR_DCRST | FLASH_ACR_ICRST;
    FLASH->ACR &= ~(FLASH_ACR_DCRST | FLASH_ACR_ICRST);
    FLASH->ACR = acr;
}

int flash_f4_unlock(void)
{
    if ((FLASH->CR & FLASH_CR_LOCK) != 0U) {
        FLASH->KEYR = F4_FLASH_KEY1;
        FLASH->KEYR = F4_FLASH_KEY2;
    }
    FLASH->SR = FLASH_ERR_FLAGS | FLASH_SR_EOP;
    return ((FLASH->CR & FLASH_CR_LOCK) != 0U) ? -1 : 0;
}

void flash_f4_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

int flash_f4_erase_sector(uint32_t sector)
{
    if (sector > 11U || wait_ready() != 0) {
        return -1;
    }
    FLASH->CR = FLASH_CR_PSIZE_1 | FLASH_CR_SER | (sector << FLASH_CR_SNB_Pos);
    FLASH->CR |= FLASH_CR_STRT;
    int rc = wait_ready();
    FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB);
    flush_caches();
    return rc;
}

int flash_f4_program(uint32_t addr, const void *data, uint32_t len)
{
    const uint8_t *src = (const uint8_t *)data;
    if ((addr & 3U) != 0U || wait_ready() != 0) {
        return -1;
    }
    FLASH->CR = FLASH_CR_PSIZE_1 | FLASH_CR_PG;
    int rc = 0;
    for (uint32_t i = 0; i < len; i += 4U) {
        uint32_t word = 0xFFFFFFFFUL;
        memcpy(&word, src + i, (len - i) >= 4U ? 4U : (len - i));
        *(volatile uint32_t *)(addr + i) = word;
        __DSB();
        if ((rc = wait_ready()) != 0) {
            break;
        }
        if (*(volatile const uint32_t *)(addr + i) != word) {
            rc = -3;
            break;
        }
    }
    FLASH->CR &= ~FLASH_CR_PG;
    return rc;
}
