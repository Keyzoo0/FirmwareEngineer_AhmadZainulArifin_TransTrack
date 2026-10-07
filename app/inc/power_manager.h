/**
 * @file  power_manager.h
 * @brief STOP-mode entry/exit with RTC wake-up, activity tracking.
 */
#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

void     power_init(void);
void     power_note_activity(void);            /* button, host traffic, GPS fix (ISR-safe) */
uint32_t power_idle_ms(void);
/** Policy: STOP only if idle >= 30 s, no GPS fix, image confirmed, no update in progress. */
bool     power_stop_allowed(bool gps_fix, bool update_active, bool boot_trial);
/** Enter STOP until RTC wake-up (5 s) or button. Returns seconds spent in STOP. */
uint32_t power_enter_stop(void);

#endif /* POWER_MANAGER_H */
