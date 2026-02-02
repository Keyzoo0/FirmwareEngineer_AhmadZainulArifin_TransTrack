/**
 ******************************************************************************
 * @file    fuel_sensor.c
 * @brief   Fuel Sensor (4-20mA) Driver Implementation
 ******************************************************************************
 */

#include "fuel_sensor.h"
#include "main.h"
#include <string.h>

/* External ADC handle (defined in main.c) */
extern ADC_HandleTypeDef hadc1;

/**
 * @brief Initialize fuel sensor ADC
 */
int fuel_sensor_init(fuel_sensor_t *sensor) {
    if (sensor == NULL) return -1;

    memset(sensor, 0, sizeof(fuel_sensor_t));

    // Initialize moving average buffer
    for (int i = 0; i < FUEL_SENSOR_SAMPLE_AVG; i++) {
        sensor->samples[i] = 0;
    }
    sensor->sample_index = 0;

    sensor->initialized = true;

    return 0;
}

/**
 * @brief Read fuel sensor level
 */
int fuel_sensor_read(fuel_sensor_t *sensor) {
    if (sensor == NULL || !sensor->initialized) return -1;

    HAL_StatusTypeDef ret;
    uint32_t adc_value;

    // Start ADC conversion
    ret = HAL_ADC_Start(&hadc1);
    if (ret != HAL_OK) return -2;

    // Poll for conversion
    ret = HAL_ADC_PollForConversion(&hadc1, 100);
    if (ret != HAL_OK) {
        HAL_ADC_Stop(&hadc1);
        return -3;
    }

    // Get ADC value
    adc_value = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    // Update moving average buffer
    sensor->samples[sensor->sample_index] = adc_value;
    sensor->sample_index = (sensor->sample_index + 1) % FUEL_SENSOR_SAMPLE_AVG;

    // Get averaged value
    uint32_t avg_adc = fuel_sensor_get_average(sensor);

    // Convert to voltage
    float voltage = fuel_adc_to_voltage(avg_adc);

    // Convert to current
    float current = fuel_voltage_to_current(voltage);

    // Validate current (should be in 4-20mA range with some tolerance)
    if (!fuel_sensor_validate_current(current)) {
        // Out of range - could indicate disconnected sensor
        sensor->data.level_percent = 0;
        sensor->data.voltage = voltage;
        sensor->data.current = current;
        sensor->data.raw_adc = avg_adc;
        sensor->data.last_update_ms = HAL_GetTick();
        return -4; // Invalid current
    }

    // Convert to fuel level percentage
    float level = fuel_current_to_level(current);

    sensor->data.level_percent = level;
    sensor->data.voltage = voltage;
    sensor->data.current = current;
    sensor->data.raw_adc = avg_adc;
    sensor->data.last_update_ms = HAL_GetTick();

    return 0;
}

/**
 * @brief Validate fuel sensor current
 */
bool fuel_sensor_validate_current(float current) {
    // 4-20mA range with 10% tolerance
    // Allow 3.6mA to 22mA to account for sensor tolerance
    return (current >= 3.6f && current <= 22.0f);
}

/**
 * @brief Get moving average of ADC samples
 */
uint32_t fuel_sensor_get_average(fuel_sensor_t *sensor) {
    if (sensor == NULL) return 0;

    uint32_t sum = 0;
    for (int i = 0; i < FUEL_SENSOR_SAMPLE_AVG; i++) {
        sum += sensor->samples[i];
    }
    return sum / FUEL_SENSOR_SAMPLE_AVG;
}

/**
 * @brief Calibrate fuel sensor at specific point
 */
int fuel_sensor_calibrate(fuel_sensor_t *sensor, fuel_calib_point_t point) {
    if (sensor == NULL || !sensor->initialized) return -1;

    // Read current value
    uint32_t adc_value;
    HAL_StatusTypeDef ret;

    ret = HAL_ADC_Start(&hadc1);
    if (ret != HAL_OK) return -2;

    ret = HAL_ADC_PollForConversion(&hadc1, 100);
    if (ret != HAL_OK) {
        HAL_ADC_Stop(&hadc1);
        return -3;
    }

    adc_value = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    // In a real application, you would store calibration values in EEPROM/Flash
    // For now, this is a placeholder that reads and verifies the expected values

    float voltage = fuel_adc_to_voltage(adc_value);
    float current = fuel_voltage_to_current(voltage);

    if (point == FUEL_CALIB_4MA) {
        // Expected ~4mA (1V with 250 ohm shunt)
        if (current < 3.5f || current > 4.5f) {
            return -4; // Out of calibration range
        }
    } else if (point == FUEL_CALIB_20MA) {
        // Expected ~20mA (5V with 250 ohm shunt)
        // Note: With 3.3V ADC max, we expect ~13.2mA max
        if (current < 12.0f || current > 14.0f) {
            return -4; // Out of calibration range
        }
    }

    return 0;
}

/**
 * @brief Check if fuel data is fresh
 */
bool fuel_sensor_is_data_fresh(const fuel_sensor_t *sensor, uint32_t timeout_ms) {
    if (sensor == NULL) return false;
    return get_elapsed_ms(sensor->data.last_update_ms) < timeout_ms;
}
