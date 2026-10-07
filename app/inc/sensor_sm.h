/**
 * @file  sensor_sm.h
 * @brief Telemetry state machine INIT -> IDLE -> READ -> TRANSMIT -> IDLE, with ERROR.
 *
 * The transition logic is a pure function so every edge is unit-tested on the host;
 * the telemetry task performs the actions belonging to each state.
 */
#ifndef SENSOR_SM_H
#define SENSOR_SM_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SM_INIT = 0,
    SM_IDLE,
    SM_READ,
    SM_TRANSMIT,
    SM_ERROR,
    SM_STATE_COUNT
} sm_state_t;

typedef enum {
    SM_EV_INIT_OK = 0,
    SM_EV_INIT_FAIL,
    SM_EV_TIMER,          /* 5 s sample period elapsed (or RTC wake-up) */
    SM_EV_READ_OK,        /* all sensors read (or at least one in degraded mode) */
    SM_EV_READ_FAIL,      /* nothing could be read this cycle */
    SM_EV_TX_DONE,
    SM_EV_TX_FAIL,
    SM_EV_RECOVERED,      /* ERROR: peripheral re-init succeeded */
    SM_EV_RECOVERY_FAIL,
    SM_EV_COUNT
} sm_event_t;

typedef struct {
    sm_state_t state;
    uint8_t    consecutive_errors;
    uint8_t    recovery_attempts;
    bool       reset_requested;   /* recovery exhausted: caller logs and resets the MCU */
} sm_ctx_t;

void        sm_init(sm_ctx_t *sm);
sm_state_t  sm_dispatch(sm_ctx_t *sm, sm_event_t ev, uint8_t max_errors, uint8_t max_recovery);
const char *sm_state_str(sm_state_t s);

#endif /* SENSOR_SM_H */
