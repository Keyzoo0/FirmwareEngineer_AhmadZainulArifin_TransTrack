/**
 ******************************************************************************
 * @file    state_machine.h
 * @brief   State Machine Header for Sensor Manager
 * @details Implements INIT, IDLE, READ, TRANSMIT, ERROR states
 ******************************************************************************
 */

#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "bme280.h"
#include "gps.h"
#include "fuel_sensor.h"

/* State Machine States */
typedef enum {
    STATE_INIT = 0,        // Initialize all peripherals and sensors
    STATE_IDLE,            // Wait for measurement interval
    STATE_READ,            // Read all sensors with timeout handling
    STATE_TRANSMIT,        // Send sensor data via UART
    STATE_ERROR,           // Error handling and recovery
    STATE_COUNT            // Number of states (for validation)
} state_t;

/* State Machine Events */
typedef enum {
    EVENT_NONE = 0,
    EVENT_INIT_COMPLETE,       // Initialization finished
    EVENT_TIMEOUT,             // Measurement interval timeout
    EVENT_DATA_READY,          // Sensor data ready
    EVENT_TRANSMIT_COMPLETE,   // Data transmission complete
    EVENT_ERROR,               // Error occurred
    EVENT_RECOVERY,            // Recovery successful
} event_t;

/* Error Codes */
typedef enum {
    ERROR_NONE = 0,
    ERROR_BME280_INIT,
    ERROR_BME280_READ,
    ERROR_GPS_INIT,
    ERROR_GPS_NO_DATA,
    ERROR_GPS_CHECKSUM,
    ERROR_FUEL_INIT,
    ERROR_FUEL_READ,
    ERROR_UART_INIT,
    ERROR_POWER_FAILURE,
    ERROR_UNKNOWN
} error_code_t;

/* State Machine Context - holds all sensor data */
typedef struct {
    /* Sensor devices */
    bme280_t bme280;
    gps_t gps;
    fuel_sensor_t fuel;

    /* Combined sensor data timestamp */
    uint32_t sensor_timestamp;

    /* Status flags */
    bool bme280_valid;
    bool gps_valid;
    bool fuel_valid;

    /* Error tracking */
    error_code_t last_error;
    uint8_t error_count;
    uint32_t last_error_time;

    /* Activity tracking for STOP mode */
    uint32_t last_gps_time;      // Last time GPS had valid data
    uint32_t last_activity_time; // Last sensor activity
} sm_context_t;

/* State Machine Structure */
typedef struct {
    state_t current_state;
    state_t previous_state;
    state_t next_state;
    uint32_t state_entry_time;
    sm_context_t context;
    bool initialized;
} state_machine_t;

/* State function pointer type */
typedef void (*state_func_t)(state_machine_t *sm);

/* Function Prototypes */

/**
 * @brief Initialize state machine
 * @param sm Pointer to state machine structure
 * @return 0 on success, negative error code on failure
 */
int state_machine_init(state_machine_t *sm);

/**
 * @brief Run state machine (call from main loop)
 * @param sm Pointer to state machine structure
 * @return 0 on success, negative error code on failure
 */
int state_machine_run(state_machine_t *sm);

/**
 * @brief Transition to new state
 * @param sm Pointer to state machine structure
 * @param new_state Target state
 * @return 0 on success, negative error code on failure
 */
int state_machine_transition(state_machine_t *sm, state_t new_state);

/**
 * @brief Trigger event in state machine
 * @param sm Pointer to state machine structure
 * @param event Event to trigger
 * @return 0 on success, negative error code on failure
 */
int state_machine_trigger_event(state_machine_t *sm, event_t event);

/**
 * @brief Get current state name as string
 * @param state State enum value
 * @return State name string
 */
const char* state_machine_get_state_name(state_t state);

/**
 * @brief Get error description
 * @param error Error code
 * @return Error description string
 */
const char* state_machine_get_error_string(error_code_t error);

/**
 * @brief Check if all sensor data is valid
 * @param sm Pointer to state machine structure
 * @return true if all sensors have valid data
 */
bool state_machine_all_sensors_valid(const state_machine_t *sm);

/**
 * @brief Check if STOP mode should be entered
 * @param sm Pointer to state machine structure
 * @param timeout_ms Timeout in milliseconds
 * @return true if STOP mode should be entered
 */
bool state_machine_should_enter_stop(const state_machine_t *sm, uint32_t timeout_ms);

/* State Handlers (internal) */
void state_init_handler(state_machine_t *sm);
void state_idle_handler(state_machine_t *sm);
void state_read_handler(state_machine_t *sm);
void state_transmit_handler(state_machine_t *sm);
void state_error_handler(state_machine_t *sm);

/* Utility Functions */

/**
 * @brief Get elapsed time in milliseconds
 * @param start_time Start timestamp from HAL_GetTick()
 * @return Elapsed time in milliseconds
 */
static inline uint32_t get_elapsed_ms(uint32_t start_time) {
    uint32_t now = HAL_GetTick();
    if (now >= start_time) {
        return now - start_time;
    }
    // Handle wraparound (approx 49 days)
    return (UINT32_MAX - start_time) + now + 1;
}

#ifdef __cplusplus
}
#endif

#endif /* STATE_MACHINE_H */
