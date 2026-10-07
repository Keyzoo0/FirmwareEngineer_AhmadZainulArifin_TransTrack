/**
 * @file  boot_control.h
 * @brief Application side of the A/B boot protocol (confirm trial image, stage an update).
 */
#ifndef BOOT_CONTROL_H
#define BOOT_CONTROL_H

#include "boot_journal.h"
#include <stdbool.h>

void    boot_control_init(void);
bool    boot_control_is_trial(void);       /* running image has not been confirmed yet */
int     boot_control_confirm(void);        /* mark running image good (after 10 s healthy) */
int     boot_control_stage_update(uint8_t slot);
uint8_t boot_control_running_slot(void);
uint8_t boot_control_inactive_slot(void);
bool    boot_control_state(boot_state_t *out);

#endif /* BOOT_CONTROL_H */
