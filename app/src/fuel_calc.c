/**
 * @file  fuel_calc.c
 * @brief 4-20 mA conversion. Integer math only (no soft-float printf needed).
 *
 * Changes compared with the February version:
 *  - the 250R shunt produces 1..5 V but went straight into the ADC (max VDDA = 3.0..3.3 V), so
 *    anything above ~13 mA saturated (the old code even noted this). A 10k/10k divider now
 *    scales it to 0.5..2.5 V; a 24 mA fault current still stays below 3.0 V
 *  - the input moved from PA0 (also the user button on the DISC1) to PA1
 *  - VDDA is measured with VREFINT instead of assuming exactly 3.3 V
 *  - "out of range" is split into open loop / short / under- / over-range (NAMUR NE43),
 *    integer math only
 */
#include "fuel_calc.h"

#define UA_FAULT_LOW     3600U
#define UA_RANGE_LOW     3800U
#define UA_ZERO          4000U
#define UA_FULL          20000U
#define UA_RANGE_HIGH    20500U
#define UA_FAULT_HIGH    21000U

fuel_reading_t fuel_calc(uint32_t raw_ch, uint32_t raw_vref, uint16_t vrefint_cal,
                         uint32_t divider_ppm, uint32_t shunt_mohm)
{
    fuel_reading_t r = {0};

    if (raw_vref == 0U || divider_ppm == 0U || shunt_mohm == 0U) {
        r.status = FUEL_FAULT_LOW;
        return r;
    }
    /* VDDA = 3.3 V * VREFINT_CAL / VREFINT_measured */
    r.vdda_mv = (3300U * (uint32_t)vrefint_cal) / raw_vref;
    r.pin_mv  = (r.vdda_mv * raw_ch) / 4095U;

    /* shunt voltage (uV) = pin_mv * 1000 / ratio ; I(uA) = V(uV) / R(ohm) = V(uV) * 1000 / R(mohm) */
    uint64_t shunt_uv = ((uint64_t)r.pin_mv * 1000000000ULL) / divider_ppm;
    r.loop_ua = (uint32_t)((shunt_uv * 1000ULL) / shunt_mohm);

    uint32_t ua = r.loop_ua;
    if (ua < UA_FAULT_LOW) {
        r.status = FUEL_FAULT_LOW;
        r.level_x10 = 0;
        return r;
    }
    if (ua > UA_FAULT_HIGH) {
        r.status = FUEL_FAULT_HIGH;
        r.level_x10 = 0;
        return r;
    }
    r.status = (ua < UA_RANGE_LOW)  ? FUEL_UNDER_RANGE :
               (ua > UA_RANGE_HIGH) ? FUEL_OVER_RANGE  : FUEL_OK;

    if (ua <= UA_ZERO) {
        r.level_x10 = 0;
    } else if (ua >= UA_FULL) {
        r.level_x10 = 1000;
    } else {
        r.level_x10 = (uint16_t)(((ua - UA_ZERO) * 1000U + (UA_FULL - UA_ZERO) / 2U) / (UA_FULL - UA_ZERO));
    }
    return r;
}

const char *fuel_status_str(fuel_status_t st)
{
    switch (st) {
    case FUEL_OK:          return "ok";
    case FUEL_FAULT_LOW:   return "open_loop";
    case FUEL_FAULT_HIGH:  return "short";
    case FUEL_UNDER_RANGE: return "under_range";
    case FUEL_OVER_RANGE:  return "over_range";
    default:               return "unknown";
    }
}
