/**
 * @file  sys_monitor.c
 * @brief Highest-priority task: the only place that refreshes the IWDG during normal operation.
 *
 * The watchdog is refreshed only if *every* registered task has reported a heartbeat within
 * HEARTBEAT_TIMEOUT_MS. A deadlocked or starved task therefore leads to a watchdog reset
 * (logged as EVT_WATCHDOG_STARVED first) instead of a system that looks alive but is not.
 *
 * Boot confirmation: an image started on trial by the bootloader is confirmed only after it
 * has run BOOT_CONFIRM_MS (10 s) with all heartbeats healthy. If it crashes, hangs or is
 * reset before that, the bootloader rolls back to the previous slot.
 */
#include "sys_monitor.h"
#include "app_config.h"
#include "boot_control.h"
#include "bsp.h"
#include "event_log.h"
#include "FreeRTOS.h"
#include "task.h"

static volatile TickType_t s_hb[HB_COUNT];

void sys_monitor_heartbeat(hb_id_t id)
{
    if (id < HB_COUNT) s_hb[id] = xTaskGetTickCount();
}

void sys_monitor_resync(void)
{
    TickType_t now = xTaskGetTickCount();
    for (int i = 0; i < HB_COUNT; i++) s_hb[i] = now;
}

uint32_t sys_monitor_uptime_s(void)
{
    return (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
}

void sys_monitor_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    TickType_t healthy_since = last;
    bool starved_logged = false;

    sys_monitor_resync();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(500));
        TickType_t now = xTaskGetTickCount();

        uint16_t stale = 0;
        for (int i = 0; i < HB_COUNT; i++) {
            if ((now - s_hb[i]) > pdMS_TO_TICKS(HEARTBEAT_TIMEOUT_MS)) stale |= (uint16_t)(1U << i);
        }

        if (stale == 0U) {
            bsp_iwdg_refresh();
            starved_logged = false;
            if (boot_control_is_trial() &&
                (now - healthy_since) >= pdMS_TO_TICKS(BOOT_CONFIRM_MS)) {
                if (boot_control_confirm() == 0) {
                    event_log_write(EVT_BOOT_CONFIRMED, (uint16_t)APP_SLOT,
                                    (FW_VERSION_MAJOR << 16) | (FW_VERSION_MINOR << 8) | FW_VERSION_PATCH);
                }
            }
        } else {
            healthy_since = now;   /* confirmation requires 10 s of *continuous* health */
            if (!starved_logged) {
                event_log_write(EVT_WATCHDOG_STARVED, stale, 0);
                starved_logged = true;
            }
            /* no refresh: the IWDG resets the MCU within 8 s */
        }
        bsp_led_toggle(LED_POWER_PIN);
    }
}
