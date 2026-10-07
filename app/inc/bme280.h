/**
 * @file  bme280.h
 * @brief BME280 temperature / pressure / humidity driver (I2C, forced mode).
 */
#ifndef BME280_H
#define BME280_H

#include <stdint.h>

typedef struct {
    int32_t  temp_c_x100;     /* 2512 = 25.12 C */
    uint32_t press_pa;        /* 101325 = 1013.25 hPa */
    uint32_t hum_x1024;       /* %RH * 1024 (datasheet Q22.10) */
} bme280_data_t;

typedef enum {
    BME280_OK = 0,
    BME280_ERR_BUS,           /* NACK / timeout / bus stuck (after recovery attempts) */
    BME280_ERR_CHIP_ID,       /* device answered but is not a BME280 (0x60) */
    BME280_ERR_TIMEOUT,       /* measurement did not complete */
    BME280_ERR_RANGE,         /* compensated value outside the sensor's physical range */
} bme280_status_t;

typedef struct {
    /* trimming parameters (datasheet 4.2.2) */
    uint16_t dig_T1; int16_t dig_T2, dig_T3;
    uint16_t dig_P1; int16_t dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
    uint8_t  dig_H1; int16_t dig_H2; uint8_t dig_H3; int16_t dig_H4, dig_H5; int8_t dig_H6;
} bme280_calib_t;

/** I2C transfer hooks so the compensation math can be unit-tested without hardware. */
typedef struct {
    int (*read)(uint8_t reg, uint8_t *buf, uint16_t len);   /* 0 = ok */
    int (*write)(uint8_t reg, uint8_t val);
    void (*delay_ms)(uint32_t ms);
    void (*bus_recover)(void);
} bme280_bus_t;

bme280_status_t bme280_init(const bme280_bus_t *bus);
bme280_status_t bme280_read(bme280_data_t *out);

/* Exposed for unit tests: datasheet reference compensation */
void    bme280_parse_calib(const uint8_t c1[26], const uint8_t c2[7], bme280_calib_t *cal);
int32_t bme280_comp_temp(const bme280_calib_t *cal, int32_t adc_T, int32_t *t_fine);
uint32_t bme280_comp_press(const bme280_calib_t *cal, int32_t adc_P, int32_t t_fine);
uint32_t bme280_comp_hum(const bme280_calib_t *cal, int32_t adc_H, int32_t t_fine);

/** HAL-backed bus for I2C1 (bme280_port.c). */
const bme280_bus_t *bme280_hal_bus(void);

#endif /* BME280_H */
