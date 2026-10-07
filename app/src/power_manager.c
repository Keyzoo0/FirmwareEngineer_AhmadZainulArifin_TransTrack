/**
 * @file  power_manager.c
 * @brief STOP mode with the low-power regulator, woken by the RTC every 5 s or by the button.
 *
 * Changes compared with the February version:
 *  - the RTC wake-up timer was never actually configured (left as a comment), so the device
 *    could only be woken by the button; it now wakes every 5 s to sample
 *  - SystemClock_Config() after wake-up was commented out: the core kept running from HSI
 *    (16 MHz) and every UART baud rate was wrong after the first STOP. Clocks are restored now
 *  - the IWDG (new) keeps running in STOP on the F407 (no freeze option): the RTC wake-up
 *    period (5 s) is shorter than the watchdog (8 s) and the watchdog is refreshed on every wake
 *  - both the HAL tick (TIM6) and the RTOS SysTick are stopped, and the RTOS tick count is
 *    caught up with the time spent in STOP (xTaskCatchUpTicks)
 */
#include "power_manager.h"
#include "app_config.h"
#include "bsp.h"
#include "gps.h"
#include "sys_monitor.h"
#include "FreeRTOS.h"
#include "task.h"

static volatile TickType_t s_last_activity;

void power_init(void)
{
    HAL_PWREx_EnableFlashPowerDown();       /* flash in deep power-down during STOP (FPDS) */
    s_last_activity = xTaskGetTickCount();
}

void power_note_activity(void)
{
    s_last_activity = (xPortIsInsideInterrupt() != pdFALSE) ? xTaskGetTickCountFromISR() : xTaskGetTickCount();
}

uint32_t power_idle_ms(void)
{
    return (uint32_t)((xTaskGetTickCount() - s_last_activity) * portTICK_PERIOD_MS);
}

bool power_stop_allowed(bool gps_fix, bool update_active, bool boot_trial)
{
    return !gps_fix && !update_active && !boot_trial && power_idle_ms() >= STOP_MODE_IDLE_MS;
}

static uint32_t rtc_seconds(void)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;
    HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);   /* must follow GetTime to unlock shadow regs */
    return (uint32_t)d.Date * 86400U + t.Hours * 3600U + t.Minutes * 60U + t.Seconds;
}

uint32_t power_enter_stop(void)
{
    /* LSI 32 kHz / 16 = 2 kHz wake-up clock -> 5 s = 10000 counts */
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
    HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, RTC_WAKEUP_PERIOD_S * 2000U - 1U, RTC_WAKEUPCLOCK_RTCCLK_DIV16);

    uint32_t t0 = rtc_seconds();
    bsp_led(LED_POWER_PIN, false);
    bsp_led(LED_GPS_PIN, false);

    vTaskSuspendAll();
    bsp_iwdg_refresh();
    HAL_SuspendTick();                                   /* TIM6 HAL tick */
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;           /* RTOS tick */
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

    /* --- woken by RTC or EXTI0; running on HSI here --- */
    bsp_system_clock_config();
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;
    HAL_ResumeTick();
    bsp_iwdg_refresh();
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);

    uint32_t slept = rtc_seconds() - t0;
    if (slept > RTC_WAKEUP_PERIOD_S) slept = RTC_WAKEUP_PERIOD_S;
    (void)xTaskResumeAll();
    (void)xTaskCatchUpTicks(pdMS_TO_TICKS(slept * 1000U));
    sys_monitor_resync();                                /* heartbeats did not run while stopped */
    gps_start();                                         /* DMA/UART state is undefined after STOP */
    bsp_led(LED_POWER_PIN, true);
    return slept;
}
