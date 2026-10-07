/**
 * @file  sensor_sm.c
 * @brief Pure state-transition logic for the telemetry state machine.
 *
 * Changes compared with the February version: the old ERROR state waited 1 s and went back
 * to INIT forever, with no escalation, and TRANSMIT never sent anything (the UART call was
 * still a TODO). Now:
 *  - a failed cycle returns to IDLE; only MAX_ERROR_COUNT consecutive failures enter ERROR
 *  - ERROR performs I2C bus recovery + re-init; after MAX_RECOVERY_ATTEMPTS it requests a
 *    controlled reset (logged), which in a trial image also triggers the rollback
 *  - the transition logic is a pure function, unit-tested on the host
 */
#include "sensor_sm.h"

void sm_init(sm_ctx_t *sm)
{
    sm->state = SM_INIT;
    sm->consecutive_errors = 0;
    sm->recovery_attempts = 0;
    sm->reset_requested = false;
}

static sm_state_t on_failure(sm_ctx_t *sm, uint8_t max_errors)
{
    if (sm->consecutive_errors < 255U) sm->consecutive_errors++;
    return (sm->consecutive_errors >= max_errors) ? SM_ERROR : SM_IDLE;
}

sm_state_t sm_dispatch(sm_ctx_t *sm, sm_event_t ev, uint8_t max_errors, uint8_t max_recovery)
{
    sm_state_t next = sm->state;

    switch (sm->state) {
    case SM_INIT:
        if (ev == SM_EV_INIT_OK)        next = SM_IDLE;
        else if (ev == SM_EV_INIT_FAIL) next = SM_ERROR;
        break;
    case SM_IDLE:
        if (ev == SM_EV_TIMER)          next = SM_READ;
        break;
    case SM_READ:
        if (ev == SM_EV_READ_OK)        next = SM_TRANSMIT;
        else if (ev == SM_EV_READ_FAIL) next = on_failure(sm, max_errors);
        break;
    case SM_TRANSMIT:
        if (ev == SM_EV_TX_DONE) {
            sm->consecutive_errors = 0;
            next = SM_IDLE;
        } else if (ev == SM_EV_TX_FAIL) {
            next = on_failure(sm, max_errors);
        }
        break;
    case SM_ERROR:
        if (ev == SM_EV_RECOVERED) {
            sm->consecutive_errors = 0;
            sm->recovery_attempts = 0;
            next = SM_IDLE;
        } else if (ev == SM_EV_RECOVERY_FAIL) {
            if (sm->recovery_attempts < 255U) sm->recovery_attempts++;
            if (sm->recovery_attempts >= max_recovery) sm->reset_requested = true;
        }
        break;
    default:
        next = SM_ERROR;
        break;
    }
    sm->state = next;
    return next;
}

const char *sm_state_str(sm_state_t s)
{
    static const char *const names[SM_STATE_COUNT] = { "INIT", "IDLE", "READ", "TRANSMIT", "ERROR" };
    return (s < SM_STATE_COUNT) ? names[s] : "?";
}
