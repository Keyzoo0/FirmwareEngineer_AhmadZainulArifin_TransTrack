/**
 ******************************************************************************
 * @file    bme280.h
 * @brief   BME280 Sensor Driver Header
 * @details I2C-based temperature, pressure, humidity sensor
 ******************************************************************************
 */

#ifndef BME280_H
#define BME280_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* BME280 Register Addresses */
#define BME280_REG_TEMP_XLSB     0xFC
#define BME280_REG_TEMP_LSB      0xFB
#define BME280_REG_TEMP_MSB      0xFA
#define BME280_REG_PRESS_XLSB    0xF9
#define BME280_REG_PRESS_LSB     0xF8
#define BME280_REG_PRESS_MSB     0xF7
#define BME280_REG_HUM_LSB       0xFE
#define BME280_REG_HUM_MSB       0xFD
#define BME280_REG_CONFIG        0xF5
#define BME280_REG_CTRL_MEAS     0xF4
#define BME280_REG_STATUS        0xF3
#define BME280_REG_CTRL_HUM      0xF2
#define BME280_REG_CALIB_00      0x88
#define BME280_REG_CALIB_01      0xA1
#define BME280_REG_CALIB_02      0xE1
#define BME280_REG_ID            0xD0
#define BME280_REG_RESET         0xE0

/* BME280 Chip ID */
#define BME280_CHIP_ID           0x60

/* BME280 Oversampling modes */
typedef enum {
    BME280_OVERSAMP_SKIP = 0,
    BME280_OVERSAMP_1X   = 1,
    BME280_OVERSAMP_2X   = 2,
    BME280_OVERSAMP_4X   = 3,
    BME280_OVERSAMP_8X   = 4,
    BME280_OVERSAMP_16X  = 5
} bme280_oversampling_t;

/* BME280 Standby time */
typedef enum {
    BME280_STANDBY_0_5   = 0,
    BME280_STANDBY_62_5  = 1,
    BME280_STANDBY_125   = 2,
    BME280_STANDBY_250   = 3,
    BME280_STANDBY_500   = 4,
    BME280_STANDBY_1000  = 5,
    BME280_STANDBY_10MS  = 6,
    BME280_STANDBY_20MS  = 7
} bme280_standby_t;

/* BME280 Filter coefficient */
typedef enum {
    BME280_FILTER_OFF = 0,
    BME280_FILTER_2   = 1,
    BME280_FILTER_4   = 2,
    BME280_FILTER_8   = 3,
    BME280_FILTER_16  = 4
} bme280_filter_t;

/* BME280 Sensor Mode */
typedef enum {
    BME280_MODE_SLEEP  = 0,
    BME280_MODE_FORCED = 1,
    BME280_MODE_NORMAL = 3
} bme280_mode_t;

/* Calibration data structure */
typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;
    uint8_t  dig_H1;
    int16_t  dig_H2;
    uint8_t  dig_H3;
    int16_t  dig_H4;
    int16_t  dig_H5;
    int8_t   dig_H6;
} bme280_calib_data_t;

/* Sensor data structure */
typedef struct {
    float temperature;    // Celsius
    float pressure;       // Pascals
    float humidity;       // % relative humidity
    uint32_t timestamp;   // milliseconds
} bme280_data_t;

/* BME280 Device structure */
typedef struct {
    uint8_t address;
    bme280_calib_data_t calib;
    bme280_data_t data;
    bool initialized;
} bme280_t;

/* Function prototypes */
/**
 * @brief Initialize BME280 sensor
 * @param dev Pointer to BME280 device structure
 * @return 0 on success, negative error code on failure
 */
int bme280_init(bme280_t *dev);

/**
 * @brief Read sensor data (forced mode)
 * @param dev Pointer to BME280 device structure
 * @return 0 on success, negative error code on failure
 */
int bme280_read_data(bme280_t *dev);

/**
 * @brief Configure BME280 settings
 * @param dev Pointer to BME280 device structure
 * @param temp_oversamp Temperature oversampling
 * @param press_oversamp Pressure oversampling
 * @param hum_oversamp Humidity oversampling
 * @param filter Filter coefficient
 * @param standby Standby time
 * @param mode Operating mode
 * @return 0 on success, negative error code on failure
 */
int bme280_configure(bme280_t *dev, bme280_oversampling_t temp_oversamp,
                     bme280_oversampling_t press_oversamp,
                     bme280_oversampling_t hum_oversamp,
                     bme280_filter_t filter,
                     bme280_standby_t standby,
                     bme280_mode_t mode);

/**
 * @brief Soft reset BME280
 * @param dev Pointer to BME280 device structure
 * @return 0 on success, negative error code on failure
 */
int bme280_soft_reset(bme280_t *dev);

/**
 * @brief Read chip ID to verify communication
 * @param dev Pointer to BME280 device structure
 * @return Chip ID (0x60 for BME280) or negative error code
 */
int bme280_read_chip_id(bme280_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* BME280_H */
