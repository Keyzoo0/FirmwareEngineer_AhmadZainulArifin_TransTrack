/**
 * @file  tasks.h
 * @brief FreeRTOS task entry points and shared flags.
 *
 *  Task        | Prio | Stack | Role
 *  ------------+------+-------+---------------------------------------------------------
 *  monitor     |  4   | 256 w | heartbeats -> IWDG, boot confirmation (sys_monitor.c)
 *  update      |  3   | 512 w | firmware update over USART1 into the inactive slot
 *  telemetry   |  2   | 512 w | sensor state machine, JSON output, STOP-mode decisions
 */
#ifndef TASKS_H
#define TASKS_H

#include <stdbool.h>

void telemetry_task(void *arg);
void update_task(void *arg);
bool update_session_active(void);

#endif /* TASKS_H */
