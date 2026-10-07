/**
 * @file  fuel_sensor.h
 * @brief 4-20 mA fuel level sender on ADC1_IN1 (PA1).
 */
#ifndef FUEL_SENSOR_H
#define FUEL_SENSOR_H

#include "fuel_calc.h"

void fuel_sensor_init(void);
/** Oversample channel + VREFINT, convert, apply moving average. Returns 0 if the ADC worked. */
int  fuel_sensor_read(fuel_reading_t *out);

#endif /* FUEL_SENSOR_H */
