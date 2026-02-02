/**
 ******************************************************************************
 * @file    fuel_sensor.h
 * @brief   Fuel Sensor (4-20mA) Driver Header using ADC
 * @details 4-20mA loop with 250 ohm shunt resistor (1-5V output)
 ******************************************************************************
 */

#ifndef FUEL_SENSOR_H
#define FUEL_SENSOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* Fuel Sensor Configuration */
#define FUEL_SENSOR_SAMPLE_AVG     10      // Moving average window
#define FUEL_SENSOR_ADC_MAX        4095    // 12-bit ADC max value
#define FUEL_SENSOR_VREF           3.3f    // Reference voltage in volts
#define FUEL_SENSOR_SHUNT_OHM      250.0f  // Shunt resistor value

/* 4-20mA Loop Current to Voltage mapping (with 250Ω shunt) */
/* 4mA = 1V, 20mA = 5V */
/* Note: ADC is 0-3.3V, so we need voltage divider for 5V range */
/* For now, assume 1-3.3V maps to corresponding fuel level range */

#define FUEL_SENSOR_VOLTAGE_MIN    1.0f    // 4mA through 250Ω = 1V
#define FUEL_SENSOR_VOLTAGE_MAX    3.3f    // ~13.2mA (full scale on 3.3V ADC)

/* Calibration options */
typedef enum {
    FUEL_CALIB_4MA   = 0,   // 4mA = empty (0%)
    FUEL_CALIB_20MA  = 100  // 20mA = full (100%)
} fuel_calib_point_t;

/* Fuel Sensor Data */
typedef struct {
    float level_percent;        // Fuel level 0-100%
    float voltage;              // Measured voltage (V)
    float current;              // Calculated current (mA)
    uint32_t raw_adc;           // Raw ADC value
    uint32_t last_update_ms;    // Last update timestamp
} fuel_data_t;

/* Fuel Sensor Device */
typedef struct {
    fuel_data_t data;
    uint32_t samples[FUEL_SENSOR_SAMPLE_AVG];  // Moving average buffer
    uint8_t sample_index;
    bool initialized;
} fuel_sensor_t;

/* Function Prototypes */

/**
 * @brief Initialize fuel sensor ADC
 * @param sensor Pointer to fuel sensor structure
 * @return 0 on success, negative error code on failure
 */
int fuel_sensor_init(fuel_sensor_t *sensor);

/**
 * @brief Read fuel sensor level
 * @param sensor Pointer to fuel sensor structure
 * @return 0 on success, negative error code on failure
 */
int fuel_sensor_read(fuel_sensor_t *sensor);

/**
 * @brief Convert ADC value to voltage
 * @param raw_adc Raw ADC value (0-4095 for 12-bit)
 * @return Voltage in volts
 */
static inline float fuel_adc_to_voltage(uint32_t raw_adc) {
    return ((float)raw_adc * FUEL_SENSOR_VREF) / (float)FUEL_SENSOR_ADC_MAX;
}

/**
 * @brief Convert voltage to current (using 250Ω shunt)
 * @param voltage Voltage in volts
 * @return Current in mA
 */
static inline float fuel_voltage_to_current(float voltage) {
    return (voltage / FUEL_SENSOR_SHUNT_OHM) * 1000.0f;
}

/**
 * @brief Convert current to fuel level percentage
 * @param current Current in mA (4-20mA range)
 * @return Fuel level 0-100%
 */
static inline float fuel_current_to_level(float current) {
    // Map 4-20mA to 0-100%
    float level = (current - 4.0f) / (20.0f - 4.0f) * 100.0f;
    if (level < 0.0f) return 0.0f;
    if (level > 100.0f) return 100.0f;
    return level;
}

/**
 * @brief Validate fuel sensor reading
 * @param current Current in mA
 * @return true if current is in valid 4-20mA range (with tolerance)
 */
bool fuel_sensor_validate_current(float current);

/**
 * @brief Get moving average of ADC samples
 * @param sensor Pointer to fuel sensor structure
 * @return Averaged ADC value
 */
uint32_t fuel_sensor_get_average(fuel_sensor_t *sensor);

/**
 * @brief Calibrate fuel sensor at specific point
 * @param sensor Pointer to fuel sensor structure
 * @param point Calibration point (4mA or 20mA)
 * @return 0 on success, negative error code on failure
 */
int fuel_sensor_calibrate(fuel_sensor_t *sensor, fuel_calib_point_t point);

/**
 * @brief Check if fuel data is fresh
 * @param sensor Pointer to fuel sensor structure
 * @param timeout_ms Timeout in milliseconds
 * @return true if data is fresh, false otherwise
 */
bool fuel_sensor_is_data_fresh(const fuel_sensor_t *sensor, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* FUEL_SENSOR_H */
