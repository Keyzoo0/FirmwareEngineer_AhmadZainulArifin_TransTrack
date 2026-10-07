/**
 * @file  bme280_port.c
 * @brief HAL I2C1 back-end for the BME280 driver.
 */
#include "bme280.h"
#include "bsp.h"
#include "app_config.h"
#include "FreeRTOS.h"
#include "task.h"

static int hal_read(uint8_t reg, uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Read(&hi2c1, BME280_I2C_ADDR << 1, reg, I2C_MEMADD_SIZE_8BIT, buf, len,
                            BME280_I2C_TIMEOUT_MS) == HAL_OK ? 0 : -1;
}

static int hal_write(uint8_t reg, uint8_t val)
{
    return HAL_I2C_Mem_Write(&hi2c1, BME280_I2C_ADDR << 1, reg, I2C_MEMADD_SIZE_8BIT, &val, 1,
                             BME280_I2C_TIMEOUT_MS) == HAL_OK ? 0 : -1;
}

static void rtos_delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1);   /* yield instead of busy-waiting */
}

static const bme280_bus_t bus = {
    .read = hal_read,
    .write = hal_write,
    .delay_ms = rtos_delay,
    .bus_recover = bsp_i2c_bus_recover,
};

const bme280_bus_t *bme280_hal_bus(void)
{
    return &bus;
}
