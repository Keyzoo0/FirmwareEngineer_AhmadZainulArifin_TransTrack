/**
 ******************************************************************************
 * @file    state_machine.c
 * @brief   State Machine Implementation for Sensor Manager
 ******************************************************************************
 */

#include "state_machine.h"
#include "main.h"
#include <stdio.h>

/* State table - maps state to handler function */
static const state_func_t state_table[STATE_COUNT] = {
    state_init_handler,
    state_idle_handler,
    state_read_handler,
    state_transmit_handler,
    state_error_handler
};

/**
 * @brief Initialize state machine
 */
int state_machine_init(state_machine_t *sm) {
    if (sm == NULL) return -1;

    memset(sm, 0, sizeof(state_machine_t));

    sm->current_state = STATE_INIT;
    sm->previous_state = STATE_INIT;
    sm->next_state = STATE_INIT;
    sm->state_entry_time = HAL_GetTick();
    sm->initialized = true;

    return 0;
}

/**
 * @brief Run state machine (call from main loop)
 */
int state_machine_run(state_machine_t *sm) {
    if (sm == NULL || !sm->initialized) return -1;

    // Check state transition
    if (sm->current_state != sm->next_state) {
        sm->previous_state = sm->current_state;
        sm->current_state = sm->next_state;
        sm->state_entry_time = HAL_GetTick();
    }

    // Execute current state handler
    if (sm->current_state < STATE_COUNT) {
        state_table[sm->current_state](sm);
        return 0;
    }

    return -2; // Invalid state
}

/**
 * @brief Transition to new state
 */
int state_machine_transition(state_machine_t *sm, state_t new_state) {
    if (sm == NULL) return -1;
    if (new_state >= STATE_COUNT) return -2;

    sm->next_state = new_state;
    return 0;
}

/**
 * @brief Trigger event in state machine
 */
int state_machine_trigger_event(state_machine_t *sm, event_t event) {
    if (sm == NULL) return -1;

    switch (event) {
        case EVENT_INIT_COMPLETE:
            state_machine_transition(sm, STATE_IDLE);
            break;
        case EVENT_TIMEOUT:
            if (sm->current_state == STATE_IDLE) {
                state_machine_transition(sm, STATE_READ);
            }
            break;
        case EVENT_DATA_READY:
            if (sm->current_state == STATE_READ) {
                state_machine_transition(sm, STATE_TRANSMIT);
            }
            break;
        case EVENT_TRANSMIT_COMPLETE:
            state_machine_transition(sm, STATE_IDLE);
            break;
        case EVENT_ERROR:
            state_machine_transition(sm, STATE_ERROR);
            break;
        case EVENT_RECOVERY:
            state_machine_transition(sm, STATE_IDLE);
            break;
        default:
            return -2;
    }

    return 0;
}

/**
 * @brief Get current state name
 */
const char* state_machine_get_state_name(state_t state) {
    static const char* state_names[] = {
        "INIT",
        "IDLE",
        "READ",
        "TRANSMIT",
        "ERROR"
    };

    if (state < STATE_COUNT) {
        return state_names[state];
    }
    return "UNKNOWN";
}

/**
 * @brief Get error description
 */
const char* state_machine_get_error_string(error_code_t error) {
    static const char* error_strings[] = {
        "No error",
        "BME280 initialization failed",
        "BME280 read failed",
        "GPS initialization failed",
        "GPS no data",
        "GPS checksum error",
        "Fuel sensor initialization failed",
        "Fuel sensor read failed",
        "UART initialization failed",
        "Power failure detected",
        "Unknown error"
    };

    if (error <= ERROR_UNKNOWN) {
        return error_strings[error];
    }
    return error_strings[ERROR_UNKNOWN];
}

/**
 * @brief Check if all sensor data is valid
 */
bool state_machine_all_sensors_valid(const state_machine_t *sm) {
    if (sm == NULL) return false;
    return sm->context.bme280_valid &&
           sm->context.gps_valid &&
           sm->context.fuel_valid;
}

/**
 * @brief Check if STOP mode should be entered
 */
bool state_machine_should_enter_stop(const state_machine_t *sm, uint32_t timeout_ms) {
    if (sm == NULL) return false;

    uint32_t gps_elapsed = get_elapsed_ms(sm->context.last_gps_time);
    uint32_t activity_elapsed = get_elapsed_ms(sm->context.last_activity_time);

    return (gps_elapsed >= timeout_ms) && (activity_elapsed >= timeout_ms);
}

/* State Handlers */

void state_init_handler(state_machine_t *sm) {
    sm_context_t *ctx = &sm->context;

    // Initialize BME280
    int ret = bme280_init(&ctx->bme280);
    if (ret != 0) {
        ctx->last_error = ERROR_BME280_INIT;
        ctx->error_count++;
        state_machine_transition(sm, STATE_ERROR);
        return;
    }

    // Initialize GPS
    ret = gps_init(&ctx->gps);
    if (ret != 0) {
        ctx->last_error = ERROR_GPS_INIT;
        ctx->error_count++;
        state_machine_transition(sm, STATE_ERROR);
        return;
    }

    // Initialize Fuel Sensor
    ret = fuel_sensor_init(&ctx->fuel);
    if (ret != 0) {
        ctx->last_error = ERROR_FUEL_INIT;
        ctx->error_count++;
        state_machine_transition(sm, STATE_ERROR);
        return;
    }

    // All initialized successfully
    ctx->bme280_valid = false;
    ctx->gps_valid = false;
    ctx->fuel_valid = false;
    ctx->last_activity_time = HAL_GetTick();

    state_machine_transition(sm, STATE_IDLE);
}

void state_idle_handler(state_machine_t *sm) {
    sm_context_t *ctx = &sm->context;
    uint32_t elapsed = get_elapsed_ms(sm->state_entry_time);

    // Check if measurement interval has elapsed
    if (elapsed >= SENSOR_READ_INTERVAL_MS) {
        state_machine_transition(sm, STATE_READ);
    }
}

void state_read_handler(state_machine_t *sm) {
    sm_context_t *ctx = &sm->context;
    bool has_error = false;

    // Read BME280
    int ret = bme280_read_data(&ctx->bme280);
    if (ret == 0) {
        ctx->bme280_valid = true;
    } else {
        ctx->bme280_valid = false;
        ctx->last_error = ERROR_BME280_READ;
        has_error = true;
    }

    // Read Fuel Sensor
    ret = fuel_sensor_read(&ctx->fuel);
    if (ret == 0) {
        ctx->fuel_valid = true;
    } else {
        ctx->fuel_valid = false;
        ctx->last_error = ERROR_FUEL_READ;
        has_error = true;
    }

    // Check GPS data freshness
    if (gps_is_data_fresh(&ctx->gps, GPS_TIMEOUT_MS)) {
        ctx->gps_valid = true;
        ctx->last_gps_time = HAL_GetTick();
    } else {
        ctx->gps_valid = false;
        // GPS timeout is not a fatal error
    }

    // Update activity timestamp
    ctx->last_activity_time = HAL_GetTick();
    ctx->sensor_timestamp = HAL_GetTick();

    if (has_error) {
        ctx->error_count++;
        if (ctx->error_count >= MAX_ERROR_COUNT) {
            state_machine_transition(sm, STATE_ERROR);
            return;
        }
    } else {
        ctx->error_count = 0; // Reset error count on success
    }

    state_machine_transition(sm, STATE_TRANSMIT);
}

void state_transmit_handler(state_machine_t *sm) {
    sm_context_t *ctx = &sm->context;

    // Create JSON output (simplified - in real app, use proper JSON library)
    char json[512];
    int len = snprintf(json, sizeof(json),
        "{\"timestamp\":%lu,"
        "\"bme280\":{\"temp\":%.2f,\"press\":%.2f,\"hum\":%.2f},"
        "\"gps\":{\"valid\":%s,\"time\":\"%02d%02d%02d.%03d\","
        "\"date\":\"%02d%02d%02d\",\"lat\":%.6f,\"lon\":%.6f,\"speed\":%.1f},"
        "\"fuel\":{\"level\":%.1f,\"voltage\":%.2f,\"current\":%.1f},"
        "\"status\":\"%s\"}\r\n",
        ctx->sensor_timestamp,
        ctx->bme280.data.temperature,
        ctx->bme280.data.pressure,
        ctx->bme280.data.humidity,
        ctx->gps.data.valid ? "true" : "false",
        ctx->gps.data.datetime.hours,
        ctx->gps.data.datetime.minutes,
        ctx->gps.data.datetime.seconds,
        ctx->gps.data.datetime.milliseconds,
        ctx->gps.data.datetime.day,
        ctx->gps.data.datetime.month,
        ctx->gps.data.datetime.year - 2000,
        ctx->gps.data.position.latitude,
        ctx->gps.data.position.longitude,
        ctx->gps.data.speed_knots,
        ctx->fuel.data.level_percent,
        ctx->fuel.data.voltage,
        ctx->fuel.data.current,
        state_machine_all_sensors_valid(sm) ? "OK" : "PARTIAL"
    );

    // Transmit via UART (would use HAL_UART_Transmit here)
    // For now, just transition back to IDLE

    state_machine_transition(sm, STATE_IDLE);
}

void state_error_handler(state_machine_t *sm) {
    sm_context_t *ctx = &sm->context;
    uint32_t elapsed = get_elapsed_ms(sm->state_entry_time);

    // Turn on error LED (would use HAL_GPIO_WritePin here)

    // Wait for recovery delay
    if (elapsed >= ERROR_RECOVERY_DELAY_MS) {
        // Attempt recovery by going back to INIT
        ctx->error_count = 0;
        state_machine_transition(sm, STATE_INIT);
    }
}
