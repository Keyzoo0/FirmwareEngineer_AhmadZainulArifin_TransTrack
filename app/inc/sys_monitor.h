/**
 * @file  sys_monitor.h
 * @brief System monitor task: task heartbeats, watchdog ownership, boot confirmation.
 */
#ifndef SYS_MONITOR_H
#define SYS_MONITOR_H

#include <stdint.h>

typedef enum {
    HB_TELEMETRY = 0,
    HB_UPDATE,
    HB_COUNT
} hb_id_t;

void sys_monitor_heartbeat(hb_id_t id);
void sys_monitor_resync(void);
void sys_monitor_task(void *arg);
uint32_t sys_monitor_uptime_s(void);

#endif /* SYS_MONITOR_H */
