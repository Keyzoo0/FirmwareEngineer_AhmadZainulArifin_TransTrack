/**
 * @file  event_log.h
 * @brief Persistent event log in flash sectors 2+3 (2 x 16 KB ping-pong pages).
 *
 * Each record uses a 32-byte slot and carries its own CRC, so a record torn by power loss is
 * skipped on read. When the active page is full the other page is erased and becomes
 * active, so the log always keeps at least the last 512 events.
 * Wear: 512 records per page, 10k erase cycles -> ~10 M events over both pages.
 */
#ifndef EVENT_LOG_H
#define EVENT_LOG_H

#include <stdint.h>

typedef enum {
    EVT_BOOT = 1,          /* arg = RCC_CSR reset flags (upper byte), data = fw version */
    EVT_FAULT,             /* data = faulting PC, arg = fault type */
    EVT_WATCHDOG_STARVED,  /* arg = bitmask of tasks that stopped reporting */
    EVT_STATE_ERROR,       /* telemetry state machine entered ERROR, arg = error flags */
    EVT_RECOVERED,
    EVT_SENSOR_FAIL,       /* arg = sensor id (1 BME280, 2 GPS, 3 fuel), data = status */
    EVT_STOP_ENTER,
    EVT_STOP_EXIT,
    EVT_UPDATE_START,      /* data = new version */
    EVT_UPDATE_STAGED,
    EVT_UPDATE_FAILED,     /* arg = up_status_t */
    EVT_BOOT_CONFIRMED,
    EVT_CONTROLLED_RESET,
} event_id_t;

typedef struct __attribute__((packed)) {
    uint32_t seq;
    uint32_t uptime_s;
    uint16_t id;
    uint16_t arg;
    uint32_t data;
} event_rec_t;

void     event_log_init(void);   /* locate active page + next sequence number */
void     event_log_write(event_id_t id, uint16_t arg, uint32_t data);   /* thread context only */
/** Iterate oldest -> newest; returns number of valid records visited. */
uint32_t event_log_dump(void (*cb)(const event_rec_t *rec, void *ctx), void *ctx);

#endif /* EVENT_LOG_H */
