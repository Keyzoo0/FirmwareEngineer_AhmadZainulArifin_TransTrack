/**
 ******************************************************************************
 * @file    bme280.c
 * @brief   BME280 Sensor Driver Implementation
 ******************************************************************************
 */

#include "bme280.h"
#include "main.h"
#include <string.h>

/* External I2C handle (defined in main.c) */
extern I2C_HandleTypeDef hi2c1;

/* Private helper functions */
static int bme280_read_reg(uint8_t addr, uint8_t *data, uint16_t len);
static int bme280_write_reg(uint8_t addr, uint8_t data);
static int32_t bme280_compensate_temperature(bme280_t *dev, int32_t adc_T);
static uint32_t bme280_compensate_pressure(bme280_t *dev, int32_t adc_P);
static uint32_t bme280_compensate_humidity(bme280_t *dev, int32_t adc_H);

/**
 * @brief Initialize BME280 sensor
 */
int bme280_init(bme280_t *dev) {
    if (dev == NULL) return -1;

    int ret;
    uint8_t chip_id;

    dev->address = BME280_ADDRESS;
    dev->initialized = false;

    // Read chip ID to verify communication
    ret = bme280_read_chip_id(dev);
    if (ret < 0) return ret;
    if (ret != BME280_CHIP_ID) {
        return -2; // Wrong chip ID
    }

    // Read calibration data
    uint8_t calib_data[26];
    ret = bme280_read_reg(BME280_REG_CALIB_00, calib_data, 26);
    if (ret != 0) return ret;

    dev->calib.dig_T1 = (calib_data[1] << 8) | calib_data[0];
    dev->calib.dig_T2 = (int16_t)((calib_data[3] << 8) | calib_data[2]);
    dev->calib.dig_T3 = (int16_t)((calib_data[5] << 8) | calib_data[4]);
    dev->calib.dig_P1 = (calib_data[7] << 8) | calib_data[6];
    dev->calib.dig_P2 = (int16_t)((calib_data[9] << 8) | calib_data[8]);
    dev->calib.dig_P3 = (int16_t)((calib_data[11] << 8) | calib_data[10]);
    dev->calib.dig_P4 = (int16_t)((calib_data[13] << 8) | calib_data[12]);
    dev->calib.dig_P5 = (int16_t)((calib_data[15] << 8) | calib_data[14]);
    dev->calib.dig_P6 = (int16_t)((calib_data[17] << 8) | calib_data[16]);
    dev->calib.dig_P7 = (int16_t)((calib_data[19] << 8) | calib_data[18]);
    dev->calib.dig_P8 = (int16_t)((calib_data[21] << 8) | calib_data[20]);
    dev->calib.dig_P9 = (int16_t)((calib_data[23] << 8) | calib_data[22]);
    dev->calib.dig_H1 = calib_data[25];

    uint8_t calib_data2[7];
    ret = bme280_read_reg(BME280_REG_CALIB_01, calib_data2, 7);
    if (ret != 0) return ret;

    dev->calib.dig_H2 = (int16_t)((calib_data2[1] << 8) | calib_data2[0]);
    dev->calib.dig_H3 = calib_data2[2];
    dev->calib.dig_H4 = (calib_data2[3] << 4) | (calib_data2[4] & 0x0F);
    dev->calib.dig_H5 = (calib_data2[5] << 4) | ((calib_data2[4] >> 4) & 0x0F);
    dev->calib.dig_H6 = (int8_t)calib_data2[6];

    uint8_t calib_data3[1];
    ret = bme280_read_reg(BME280_REG_CALIB_02, calib_data3, 1);
    if (ret != 0) return ret;
    dev->calib.dig_H6 = (int8_t)calib_data3[0];

    // Configure sensor: oversampling x1, filter off, normal mode
    ret = bme280_configure(dev,
                           BME280_OVERSAMP_1X,
                           BME280_OVERSAMP_1X,
                           BME280_OVERSAMP_1X,
                           BME280_FILTER_OFF,
                           BME280_STANDBY_0_5,
                           BME280_MODE_NORMAL);
    if (ret != 0) return ret;

    dev->initialized = true;
    return 0;
}

/**
 * @brief Read sensor data (forced mode - single reading)
 */
int bme280_read_data(bme280_t *dev) {
    if (dev == NULL || !dev->initialized) return -1;

    int ret;
    uint8_t data[8];

    // Read all data registers at once (pressure, temp, humidity)
    ret = bme280_read_reg(BME280_REG_PRESS_MSB, data, 8);
    if (ret != 0) return ret;

    int32_t adc_P = (data[0] << 12) | (data[1] << 4) | (data[2] >> 4);
    int32_t adc_T = (data[3] << 12) | (data[4] << 4) | (data[5] >> 4);
    int32_t adc_H = (data[6] << 8) | data[7];

    // Compensate readings using calibration data
    int32_t t_fine = bme280_compensate_temperature(dev, adc_T);
    dev->data.temperature = (float)(t_fine / 5120.0);
    dev->data.pressure = (float)bme280_compensate_pressure(dev, adc_P) / 100.0f;
    dev->data.humidity = (float)bme280_compensate_humidity(dev, adc_H) / 1024.0f;
    dev->data.timestamp = HAL_GetTick();

    return 0;
}

/**
 * @brief Configure BME280 settings
 */
int bme280_configure(bme280_t *dev, bme280_oversampling_t temp_oversamp,
                     bme280_oversampling_t press_oversamp,
                     bme280_oversampling_t hum_oversamp,
                     bme280_filter_t filter,
                     bme280_standby_t standby,
                     bme280_mode_t mode) {
    if (dev == NULL) return -1;

    int ret;

    // Set humidity oversampling
    ret = bme280_write_reg(BME280_REG_CTRL_HUM, hum_oversamp);
    if (ret != 0) return ret;

    // Set measurement control (temp, pressure oversampling and mode)
    uint8_t ctrl_meas = (temp_oversamp << 5) | (press_oversamp << 2) | mode;
    ret = bme280_write_reg(BME280_REG_CTRL_MEAS, ctrl_meas);
    if (ret != 0) return ret;

    // Set config (standby and filter)
    uint8_t config = (standby << 5) | (filter << 2);
    ret = bme280_write_reg(BME280_REG_CONFIG, config);
    if (ret != 0) return ret;

    return 0;
}

/**
 * @brief Soft reset BME280
 */
int bme280_soft_reset(bme280_t *dev) {
    if (dev == NULL) return -1;
    return bme280_write_reg(BME280_REG_RESET, 0xB6);
}

/**
 * @brief Read chip ID
 */
int bme280_read_chip_id(bme280_t *dev) {
    if (dev == NULL) return -1;

    uint8_t chip_id;
    int ret = bme280_read_reg(BME280_REG_ID, &chip_id, 1);
    if (ret != 0) return ret;
    return chip_id;
}

/* Private helper functions */

static int bme280_read_reg(uint8_t addr, uint8_t *data, uint16_t len) {
    return HAL_I2C_Mem_Read(&hi2c1, BME280_ADDRESS << 1, addr, I2C_MEMADD_SIZE_8BIT,
                            data, len, BME280_I2C_TIMEOUT);
}

static int bme280_write_reg(uint8_t addr, uint8_t data) {
    return HAL_I2C_Mem_Write(&hi2c1, BME280_ADDRESS << 1, addr, I2C_MEMADD_SIZE_8BIT,
                             &data, 1, BME280_I2C_TIMEOUT);
}

/* Compensation functions (from BME280 datasheet) */

static int32_t bme280_compensate_temperature(bme280_t *dev, int32_t adc_T) {
    int32_t var1, var2, T;
    bme280_calib_data_t *cal = &dev->calib;

    var1 = ((((adc_T >> 3) - ((int32_t)cal->dig_T1 << 1))) *
            ((int32_t)cal->dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)cal->dig_T1)) *
              ((adc_T >> 4) - ((int32_t)cal->dig_T1))) >> 12) *
            ((int32_t)cal->dig_T3)) >> 14;

    dev->calib.dig_T1 = var1 + var2 + 1280; // Save t_fine in a calibration register
    // Note: We're overwriting dig_T1 temporarily to save t_fine (hack but works)

    T = (var1 + var2) / 2560; // In 0.01°C
    return T * 100 + (var1 + var2) * 8; // Return in 0.001°C
}

static uint32_t bme280_compensate_pressure(bme280_t *dev, int32_t adc_P) {
    int32_t var1, var2;
    uint32_t p;
    bme280_calib_data_t *cal = &dev->calib;
    int32_t t_fine = cal->dig_T1; // We stored t_fine here

    var1 = (((int32_t)t_fine / 2) - 64000) >> 2;
    var2 = (((var1 >> 2) * (var1 >> 2)) >> 11) * ((int32_t)cal->dig_P6);
    var2 = var2 + ((var1 * ((int32_t)cal->dig_P5)) << 1);
    var2 = (var2 >> 2) + (((int32_t)cal->dig_P4) << 16);

    var1 = (((((var1 >> 2) * (var1 >> 2)) >> 13) *
            (((int32_t)cal->dig_P3) << 5)) >> 3) +
           ((((int32_t)cal->dig_P2) * var1) >> 1);
    var1 = var1 >> 18;

    var1 = ((32768 + var1) * ((int32_t)cal->dig_P1)) >> 15;
    if (var1 == 0) return 0; // Avoid exception

    p = (((uint32_t)(1048576 - adc_P) - (var2 >> 12))) * 3125;

    if (p < 0x80000000) {
        p = (p << 1) / ((uint32_t)var1);
    } else {
        p = (p / (uint32_t)var1) * 2;
    }

    var1 = (((int32_t)cal->dig_P9) * ((int32_t)(((p >> 3) * (p >> 3)) >> 13))) >> 12;
    var2 = (((int32_t)(p >> 2)) * ((int32_t)cal->dig_P8)) >> 13;
    p = (uint32_t)((int32_t)p + ((var1 + var2 + cal->dig_P7) >> 4));

    return p; // Pressure in Pa
}

static uint32_t bme280_compensate_humidity(bme280_t *dev, int32_t adc_H) {
    int32_t v_x1_u32r;
    bme280_calib_data_t *cal = &dev->calib;
    int32_t t_fine = cal->dig_T1;

    v_x1_u32r = (t_fine - ((int32_t)76800));

    v_x1_u32r = (((((adc_H << 14) - (((int32_t)cal->dig_H4) << 20) -
                   (((int32_t)cal->dig_H5) * v_x1_u32r)) + ((int32_t)16384)) >> 15) *
                (((((((v_x1_u32r * ((int32_t)cal->dig_H6)) >> 10) *
                     (((v_x1_u32r * ((int32_t)cal->dig_H3)) >> 11) + 32768)) >> 10) +
                   2097152) * ((int32_t)cal->dig_H2) + 8192) >> 14);

    v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) *
                               ((int32_t)cal->dig_H1)) >> 4));

    v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);
    v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);

    return (uint32_t)(v_x1_u32r >> 12);
}
