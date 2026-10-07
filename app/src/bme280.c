/**
 * @file  bme280.c
 * @brief BME280 driver: forced-mode measurement + Bosch reference integer compensation.
 *
 * Changes compared with the February version:
 *  - temperature compensation fixed: the old code returned a wrong scale and stored t_fine in
 *    dig_T1, i.e. it overwrote a calibration constant, so every later reading was wrong
 *  - a failed transfer (100 ms timeout) now triggers I2C bus recovery and up to 3 retries
 *    instead of only returning an error while SDA may still be held low by the sensor
 *  - dig_H4/dig_H5 are sign-extended as in the Bosch reference code
 *  - the measurement waits on the "measuring" status bit with a bound, not a fixed delay
 *  - compensated values are range-checked; formulas checked against the datasheet example
 *    in tests/test_main.c
 */
#include "bme280.h"
#include "app_config.h"
#include <string.h>

#define REG_CHIP_ID     0xD0
#define REG_RESET       0xE0
#define REG_CALIB00     0x88   /* 0x88..0xA1 : 26 bytes */
#define REG_CALIB26     0xE1   /* 0xE1..0xE7 : 7 bytes */
#define REG_CTRL_HUM    0xF2
#define REG_STATUS      0xF3
#define REG_CTRL_MEAS   0xF4
#define REG_CONFIG      0xF5
#define REG_DATA        0xF7   /* 0xF7..0xFE : 8 bytes */
#define CHIP_ID         0x60

static const bme280_bus_t *s_bus;
static bme280_calib_t s_cal;

static int xfer_read(uint8_t reg, uint8_t *buf, uint16_t len)
{
    for (uint32_t attempt = 0; attempt < BME280_MAX_RETRIES; attempt++) {
        if (s_bus->read(reg, buf, len) == 0) return 0;
        if (s_bus->bus_recover != NULL) s_bus->bus_recover();
    }
    return -1;
}

static int xfer_write(uint8_t reg, uint8_t val)
{
    for (uint32_t attempt = 0; attempt < BME280_MAX_RETRIES; attempt++) {
        if (s_bus->write(reg, val) == 0) return 0;
        if (s_bus->bus_recover != NULL) s_bus->bus_recover();
    }
    return -1;
}

void bme280_parse_calib(const uint8_t c[26], const uint8_t h[7], bme280_calib_t *k)
{
    k->dig_T1 = (uint16_t)(c[0] | (c[1] << 8));
    k->dig_T2 = (int16_t)(c[2] | (c[3] << 8));
    k->dig_T3 = (int16_t)(c[4] | (c[5] << 8));
    k->dig_P1 = (uint16_t)(c[6] | (c[7] << 8));
    k->dig_P2 = (int16_t)(c[8] | (c[9] << 8));
    k->dig_P3 = (int16_t)(c[10] | (c[11] << 8));
    k->dig_P4 = (int16_t)(c[12] | (c[13] << 8));
    k->dig_P5 = (int16_t)(c[14] | (c[15] << 8));
    k->dig_P6 = (int16_t)(c[16] | (c[17] << 8));
    k->dig_P7 = (int16_t)(c[18] | (c[19] << 8));
    k->dig_P8 = (int16_t)(c[20] | (c[21] << 8));
    k->dig_P9 = (int16_t)(c[22] | (c[23] << 8));
    k->dig_H1 = c[25];                                  /* 0xA1 */
    k->dig_H2 = (int16_t)(h[0] | (h[1] << 8));          /* 0xE1/0xE2 */
    k->dig_H3 = h[2];                                   /* 0xE3 */
    k->dig_H4 = (int16_t)(((int16_t)(int8_t)h[3] * 16) | (h[4] & 0x0F));   /* 0xE4[11:4], 0xE5[3:0] */
    k->dig_H5 = (int16_t)(((int16_t)(int8_t)h[5] * 16) | (h[4] >> 4));     /* 0xE6[11:4], 0xE5[7:4] */
    k->dig_H6 = (int8_t)h[6];
}

/* Datasheet 4.2.3, BME280_compensate_T_int32: returns 0.01 degC */
int32_t bme280_comp_temp(const bme280_calib_t *k, int32_t adc_T, int32_t *t_fine)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)k->dig_T1 << 1))) * ((int32_t)k->dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)k->dig_T1)) * ((adc_T >> 4) - ((int32_t)k->dig_T1))) >> 12) *
                    ((int32_t)k->dig_T3)) >> 14;
    *t_fine = var1 + var2;
    return (*t_fine * 5 + 128) >> 8;
}

/* Datasheet BME280_compensate_P_int64: returns Pa (Q24.8 / 256).
 * Left shifts of possibly negative values are written as multiplications (shift of a negative
 * signed value is undefined behaviour in C); the compiler emits the same shift instructions. */
uint32_t bme280_comp_press(const bme280_calib_t *k, int32_t adc_P, int32_t t_fine)
{
    int64_t var1 = ((int64_t)t_fine) - 128000;
    int64_t var2 = var1 * var1 * (int64_t)k->dig_P6;
    var2 = var2 + ((var1 * (int64_t)k->dig_P5) * 131072);          /* << 17, signed-safe */
    var2 = var2 + (((int64_t)k->dig_P4) * 34359738368LL);         /* << 35 */
    var1 = ((var1 * var1 * (int64_t)k->dig_P3) >> 8) + ((var1 * (int64_t)k->dig_P2) * 4096);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)k->dig_P1) >> 33;
    if (var1 == 0) {
        return 0;   /* avoid division by zero */
    }
    int64_t p = 1048576 - adc_P;
    p = (((p * 2147483648LL) - var2) * 3125) / var1;               /* << 31 */
    var1 = (((int64_t)k->dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)k->dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)k->dig_P7) * 16);
    return (uint32_t)(p >> 8);
}

/* Datasheet bme280_compensate_H_int32: returns %RH * 1024 */
uint32_t bme280_comp_hum(const bme280_calib_t *k, int32_t adc_H, int32_t t_fine)
{
    int32_t v = t_fine - ((int32_t)76800);
    v = (((((adc_H << 14) - (((int32_t)k->dig_H4) * 1048576) - (((int32_t)k->dig_H5) * v)) + ((int32_t)16384)) >> 15) *
         (((((((v * ((int32_t)k->dig_H6)) >> 10) * (((v * ((int32_t)k->dig_H3)) >> 11) + ((int32_t)32768))) >> 10) +
            ((int32_t)2097152)) * ((int32_t)k->dig_H2) + 8192) >> 14));
    v = (v - (((((v >> 15) * (v >> 15)) >> 7) * ((int32_t)k->dig_H1)) >> 4));
    v = (v < 0) ? 0 : v;
    v = (v > 419430400) ? 419430400 : v;
    return (uint32_t)(v >> 12);
}

bme280_status_t bme280_init(const bme280_bus_t *bus)
{
    uint8_t id = 0, c1[26], c2[7];
    s_bus = bus;

    if (xfer_read(REG_CHIP_ID, &id, 1) != 0) return BME280_ERR_BUS;
    if (id != CHIP_ID) return BME280_ERR_CHIP_ID;

    if (xfer_write(REG_RESET, 0xB6) != 0) return BME280_ERR_BUS;
    s_bus->delay_ms(3);                                /* start-up time 2 ms */

    if (xfer_read(REG_CALIB00, c1, sizeof(c1)) != 0) return BME280_ERR_BUS;
    if (xfer_read(REG_CALIB26, c2, sizeof(c2)) != 0) return BME280_ERR_BUS;
    bme280_parse_calib(c1, c2, &s_cal);

    /* Weather-monitoring profile (datasheet 3.5.1): x1 oversampling, filter off, forced mode.
     * ctrl_hum only takes effect after a write to ctrl_meas, so the order matters. */
    if (xfer_write(REG_CTRL_HUM, 0x01) != 0) return BME280_ERR_BUS;
    if (xfer_write(REG_CONFIG, 0x00) != 0) return BME280_ERR_BUS;
    if (xfer_write(REG_CTRL_MEAS, (1 << 5) | (1 << 2) | 0x00) != 0) return BME280_ERR_BUS;  /* sleep */
    return BME280_OK;
}

bme280_status_t bme280_read(bme280_data_t *out)
{
    uint8_t st = 0, d[8];
    if (s_bus == NULL) return BME280_ERR_BUS;

    /* Trigger one forced conversion: osrs_t x1, osrs_p x1, mode = 01 */
    if (xfer_write(REG_CTRL_MEAS, (1 << 5) | (1 << 2) | 0x01) != 0) return BME280_ERR_BUS;

    /* t_measure max for x1/x1/x1 = 9.3 ms; poll status.measuring with a 50 ms bound */
    uint32_t waited = 0;
    do {
        s_bus->delay_ms(2);
        waited += 2;
        if (xfer_read(REG_STATUS, &st, 1) != 0) return BME280_ERR_BUS;
    } while ((st & 0x08U) != 0U && waited < 50U);
    if ((st & 0x08U) != 0U) return BME280_ERR_TIMEOUT;

    if (xfer_read(REG_DATA, d, sizeof(d)) != 0) return BME280_ERR_BUS;
    int32_t adc_P = (int32_t)(((uint32_t)d[0] << 12) | ((uint32_t)d[1] << 4) | (d[2] >> 4));
    int32_t adc_T = (int32_t)(((uint32_t)d[3] << 12) | ((uint32_t)d[4] << 4) | (d[5] >> 4));
    int32_t adc_H = (int32_t)(((uint32_t)d[6] << 8) | d[7]);
    if (adc_T == 0x80000 || adc_P == 0x80000) return BME280_ERR_RANGE;   /* "skipped" pattern */

    int32_t t_fine;
    out->temp_c_x100 = bme280_comp_temp(&s_cal, adc_T, &t_fine);
    out->press_pa    = bme280_comp_press(&s_cal, adc_P, t_fine);
    out->hum_x1024   = bme280_comp_hum(&s_cal, adc_H, t_fine);

    /* Operating range: -40..85 C, 300..1100 hPa */
    if (out->temp_c_x100 < -4000 || out->temp_c_x100 > 8500 ||
        out->press_pa < 30000U || out->press_pa > 110000U) {
        return BME280_ERR_RANGE;
    }
    return BME280_OK;
}
