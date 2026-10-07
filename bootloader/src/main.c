/**
 * @file  main.c (bootloader)
 * @brief Dual-slot bootloader with CRC-32 + SHA-256 verification and automatic rollback.
 *
 * Boot flow (see docs/ARCHITECTURE.md for the full diagram):
 *   1. Verify both slots (header CRC, image CRC-32, SHA-256, vector table).
 *   2. Read the boot journal from sector 4 and run boot_decide():
 *        - pending update + valid      -> mark trial, jump to the new slot
 *        - trial already started       -> the new image never confirmed (watchdog /
 *                                         fault / power loss before 10 s) -> roll back
 *        - active slot corrupt         -> fall back to the other valid slot
 *   3. Start the independent watchdog (8 s) *before* jumping, so a new image that
 *      hangs during init still gets reset and rolled back.
 *   4. Relocate VTOR and jump.
 *
 * Runs from the 16 MHz HSI and touches only USART1 (PA9, 115200 8N1) and the
 * Discovery LEDs, so it has no clock-tree dependencies that could fail.
 */
#include "stm32f4xx.h"
#include "boot_journal.h"
#include "boot_journal_f4.h"
#include "flash_layout.h"
#include "fw_image.h"
#include <stdio.h>
#include <string.h>

#define BL_VERSION          "1.0.0"
#define SRAM_START          0x20000000UL
#define SRAM_END            0x20020000UL       /* 128 KB SRAM1+SRAM2, CCM is not valid for MSP here */
#define IWDG_RELOAD_8S      1000U              /* LSI 32 kHz / 256 = 125 Hz -> 8 s */

#define LED_GREEN           (1U << 12)
#define LED_ORANGE          (1U << 13)
#define LED_RED             (1U << 14)
#define LED_BLUE            (1U << 15)

/* ------------------------------------------------------------------ console */
static void uart_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIODEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;

    /* PA9 = USART1_TX, AF7 */
    GPIOA->MODER   = (GPIOA->MODER & ~(3U << (9 * 2))) | (2U << (9 * 2));
    GPIOA->AFR[1]  = (GPIOA->AFR[1] & ~(0xFU << ((9 - 8) * 4))) | (7U << ((9 - 8) * 4));

    USART1->BRR = (16000000U + 115200U / 2U) / 115200U;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE;

    /* PD12..PD15 outputs */
    GPIOD->MODER = (GPIOD->MODER & 0x00FFFFFFU) | 0x55000000U;
}

static void uart_puts(const char *s)
{
    while (*s != '\0') {
        while ((USART1->SR & USART_SR_TXE) == 0U) { }
        USART1->DR = (uint8_t)*s++;
    }
    while ((USART1->SR & USART_SR_TC) == 0U) { }
}

static void logf_(const char *fmt, unsigned a, unsigned b, unsigned c)
{
    char buf[96];
    snprintf(buf, sizeof(buf), fmt, a, b, c);
    uart_puts(buf);
}

static void leds(uint32_t on_mask)
{
    GPIOD->BSRR = ((LED_GREEN | LED_ORANGE | LED_RED | LED_BLUE) << 16) | on_mask;
}

/* ------------------------------------------------------------------ helpers */
static void iwdg_start_8s(void)
{
    IWDG->KR  = 0xCCCCU;            /* start (also starts LSI) */
    IWDG->KR  = 0x5555U;            /* unlock PR/RLR */
    IWDG->PR  = 6U;                 /* /256 */
    IWDG->RLR = IWDG_RELOAD_8S - 1U;
    while (IWDG->SR != 0U) { }
    IWDG->KR  = 0xAAAAU;            /* reload */
}

static void delay_cycles(volatile uint32_t n)
{
    while (n-- != 0U) { __NOP(); }
}

static void verify_slot(uint8_t slot, slot_info_t *info)
{
    fw_image_header_t hdr;
    fw_image_status_t st = fw_image_verify((const uint8_t *)slot_base(slot), slot_base(slot), SLOT_SIZE, slot,
                                           SRAM_START, SRAM_END, true, &hdr);
    info->valid = (st == FW_IMAGE_OK);
    info->version = info->valid ? fw_image_version(&hdr) : 0U;
    if (info->valid) {
        logf_("[BL] slot %c: v%u.%u", 'A' + slot, hdr.ver_major, hdr.ver_minor);
        logf_(".%u size=%u ok\r\n", hdr.ver_patch, hdr.image_size, 0);
    } else {
        logf_("[BL] slot %c: invalid\r\n", 'A' + slot, 0, 0);
        uart_puts("     reason: ");
        uart_puts(fw_image_status_str(st));
        uart_puts("\r\n");
    }
}

__attribute__((noreturn)) static void jump_to_slot(uint8_t slot)
{
    uint32_t vtor = slot_base(slot) + IMAGE_HEADER_SIZE;
    uint32_t sp   = *(volatile const uint32_t *)vtor;
    uint32_t pc   = *(volatile const uint32_t *)(vtor + 4U);

    __disable_irq();
    SysTick->CTRL = 0;
    for (uint32_t i = 0; i < 8U; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }
    /* Hand the peripherals back in reset state; the app configures its own clocks/pins */
    RCC->APB2RSTR |= RCC_APB2RSTR_USART1RST;
    RCC->APB2RSTR &= ~RCC_APB2RSTR_USART1RST;
    RCC->APB2ENR  &= ~RCC_APB2ENR_USART1EN;

    SCB->VTOR = vtor;
    __set_MSP(sp);
    __DSB();
    __ISB();
    __enable_irq();
    ((void (*)(void))pc)();
    for (;;) { }
}

__attribute__((noreturn)) static void recovery_loop(void)
{
    uart_puts("[BL] no bootable image - waiting for reflash via SWD\r\n");
    for (;;) {
        leds(LED_RED);
        delay_cycles(1600000U);
        leds(0);
        delay_cycles(1600000U);
        /* IWDG is not started here: staying alive in recovery is the safe state */
    }
}

/* ------------------------------------------------------------------ main */
int main(void)
{
    uart_init();
    leds(LED_GREEN | LED_ORANGE);
    uart_puts("\r\n[BL] TransTRACK bootloader " BL_VERSION "\r\n");
    logf_("[BL] reset cause RCC_CSR=0x%08x\r\n", (unsigned)RCC->CSR, 0, 0);
    /* RCC_CSR is intentionally NOT cleared: the application logs the reset cause */

    slot_info_t slots[2];
    verify_slot(SLOT_A, &slots[SLOT_A]);
    verify_slot(SLOT_B, &slots[SLOT_B]);

    const journal_flash_t *jf = boot_journal_f4();
    boot_state_t st;
    bool have_state = journal_read(jf, &st);
    if (have_state) {
        logf_("[BL] journal: active=%c pending=%u trial=%u\r\n",
              'A' + st.active_slot, st.pending_slot, st.trial_active);
    } else {
        uart_puts("[BL] journal empty - rebuilding from slot contents\r\n");
    }

    boot_decision_t d = boot_decide(have_state, have_state ? &st : NULL, slots);
    bool transition = d.write_state;               /* boot_decide() only asks to write on a state change */
    d.new_state.boot_count++;
    d.write_state = d.write_state || have_state;   /* boot_count is persisted every boot */

    switch (transition ? d.new_state.last_event : BOOT_EVT_NONE) {
    case BOOT_EVT_ROLLED_BACK:
        uart_puts("[BL] trial image did not confirm within 10 s -> ROLLBACK\r\n");
        leds(LED_RED | LED_ORANGE);
        break;
    case BOOT_EVT_FACTORY:
        uart_puts("[BL] journal initialised from slot contents\r\n");
        break;
    case BOOT_EVT_PENDING_INVALID:
        uart_puts("[BL] staged update failed verification -> discarded\r\n");
        break;
    case BOOT_EVT_FALLBACK:
        uart_puts("[BL] active slot corrupt -> fallback to other slot\r\n");
        break;
    case BOOT_EVT_TRIAL_STARTED:
        uart_puts("[BL] starting trial boot of new image\r\n");
        break;
    default:
        break;
    }

    if (d.write_state && journal_write(jf, &d.new_state) != 0) {
        /* Not fatal: booting the known-good slot is still better than staying down */
        uart_puts("[BL] WARNING: journal write failed\r\n");
    }

    if (d.boot_slot == SLOT_NONE) {
        recovery_loop();
    }

    logf_("[BL] booting slot %c (boot #%u)\r\n", 'A' + d.boot_slot, d.new_state.boot_count, 0);
    iwdg_start_8s();
    leds(LED_GREEN);
    jump_to_slot(d.boot_slot);
}
