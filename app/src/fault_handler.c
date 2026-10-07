/**
 * @file  fault_handler.c
 * @brief HardFault / MemManage / BusFault / UsageFault capture + RTOS failure hooks.
 *
 * Writing flash from inside a fault handler is unsafe (the fault may be *caused* by the flash
 * driver, and an erase could be needed). The handler therefore stores PC, LR, CFSR and the
 * fault type in the RTC backup registers, which survive a system reset, and resets.
 * On the next boot fault_log_flush() moves the record into the persistent flash log.
 *
 * A fault in a trial image resets before the 10 s confirmation -> automatic rollback.
 */
#include "bsp.h"
#include "event_log.h"
#include "FreeRTOS.h"
#include "task.h"

#define FAULT_MAGIC 0xFA017ED0UL

enum { FT_HARD = 1, FT_MEM, FT_BUS, FT_USAGE, FT_STACK_OVF, FT_MALLOC, FT_ASSERT };

static void save_and_reset(uint32_t type, uint32_t pc, uint32_t lr) __attribute__((noreturn));
static void save_and_reset(uint32_t type, uint32_t pc, uint32_t lr)
{
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR |= PWR_CR_DBP;
    RTC->BKP0R = FAULT_MAGIC;
    RTC->BKP1R = type;
    RTC->BKP2R = pc;
    RTC->BKP3R = lr;
    RTC->BKP4R = SCB->CFSR;
    RTC->BKP5R = SCB->MMFAR;
    __DSB();
    NVIC_SystemReset();
    for (;;) { }
}

/* Called from the naked handlers with the exception stack frame */
void fault_dispatch(uint32_t *frame, uint32_t type)
{
    save_and_reset(type, frame[6], frame[5]);   /* stacked PC, LR */
}

#define FAULT_ENTRY(name, type)                                   \
    __attribute__((naked)) void name(void)                        \
    {                                                             \
        __asm volatile(                                           \
            "tst lr, #4      \n"                                  \
            "ite eq          \n"                                  \
            "mrseq r0, msp   \n"                                  \
            "mrsne r0, psp   \n"                                  \
            "mov r1, %0      \n"                                  \
            "b fault_dispatch\n" :: "i"(type));                   \
    }

FAULT_ENTRY(HardFault_Handler,  FT_HARD)
FAULT_ENTRY(MemManage_Handler,  FT_MEM)
FAULT_ENTRY(BusFault_Handler,   FT_BUS)
FAULT_ENTRY(UsageFault_Handler, FT_USAGE)

void fault_log_flush(void)
{
    if (RTC->BKP0R != FAULT_MAGIC) return;
    event_log_write(EVT_FAULT, (uint16_t)RTC->BKP1R, RTC->BKP2R);
    HAL_PWR_EnableBkUpAccess();
    RTC->BKP0R = 0;
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task; (void)name;
    save_and_reset(FT_STACK_OVF, 0, (uint32_t)(uintptr_t)__builtin_return_address(0));
}

void vApplicationMallocFailedHook(void)
{
    save_and_reset(FT_MALLOC, 0, (uint32_t)(uintptr_t)__builtin_return_address(0));
}

void app_assert_failed(const char *file, int line)
{
    (void)file;
    save_and_reset(FT_ASSERT, (uint32_t)line, (uint32_t)(uintptr_t)__builtin_return_address(0));
}
