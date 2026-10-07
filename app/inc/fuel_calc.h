/**
 * @file  fuel_calc.h
 * @brief 4-20 mA fuel sender conversion with VREFINT compensation and NAMUR NE43 fault
 *        detection (pure math, unit-tested on host).
 */
#ifndef FUEL_CALC_H
#define FUEL_CALC_H

#include <stdint.h>

typedef enum {
    FUEL_OK = 0,
    FUEL_FAULT_LOW,       /* < 3.6 mA : open loop / broken wire / sensor unpowered */
    FUEL_FAULT_HIGH,      /* > 21.0 mA: short circuit / sensor fault */
    FUEL_UNDER_RANGE,     /* 3.6 .. 3.8 mA: below measuring range, clamped to 0 % */
    FUEL_OVER_RANGE,      /* 20.5 .. 21 mA: above measuring range, clamped to 100 % */
} fuel_status_t;

typedef struct {
    uint32_t      vdda_mv;
    uint32_t      pin_mv;
    uint32_t      loop_ua;      /* loop current in microamps */
    uint16_t      level_x10;    /* 0..1000 = 0.0..100.0 % */
    fuel_status_t status;
} fuel_reading_t;

/**
 * @param raw_ch       averaged 12-bit ADC count of the fuel channel
 * @param raw_vref     averaged 12-bit ADC count of VREFINT
 * @param vrefint_cal  factory VREFINT count measured at 3.3 V (0x1FFF7A2A on F407)
 * @param divider_ppm  divider ratio * 1e6 (pin voltage / shunt voltage)
 * @param shunt_mohm   shunt resistance in milliohms
 */
fuel_reading_t fuel_calc(uint32_t raw_ch, uint32_t raw_vref, uint16_t vrefint_cal,
                         uint32_t divider_ppm, uint32_t shunt_mohm);

const char *fuel_status_str(fuel_status_t st);

#endif /* FUEL_CALC_H */
