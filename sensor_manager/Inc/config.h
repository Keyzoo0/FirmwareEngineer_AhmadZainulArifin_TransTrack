/**
 ******************************************************************************
 * @file    config.h
 * @brief   Configuration constants for Sensor Manager
 * @author  Generated for STM32F407VGTx
 ******************************************************************************
 */

#ifndef CONFIG_H
#define CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Version Information */
#define FIRMWARE_VERSION_MAJOR    1
#define FIRMWARE_VERSION_MINOR    0
#define FIRMWARE_VERSION_PATCH    0
#define FIRMWARE_VERSION_STRING   "1.0.0"

/* Hardware Configuration - STM32F407G-DISC1 */

/* I2C1 - BME280 */
#define BME280_I2C                I2C1
#define BME280_I2C_TIMEOUT        100         // ms
#define BME280_ADDRESS            0x76        // Default address

/* USART2 - GPS */
#define GPS_UART                  USART2
#define GPS_UART_BAUDRATE         9600
#define GPS_BUFFER_SIZE           256
#define GPS_TIMEOUT_MS            5000

/* UART1 - Data Output */
#define DATA_UART                 USART1
#define DATA_UART_BAUDRATE        115200
#define DATA_BUFFER_SIZE          512

/* ADC1 - Fuel Sensor */
#define FUEL_ADC                  ADC1
#define FUEL_ADC_CHANNEL          ADC_CHANNEL_0
#define FUEL_ADC_RANK             1
#define FUEL_SHUNT_OHM            250.0f      // 250 ohm shunt
#define FUEL_SAMPLE_AVG           10          // Moving average samples
#define FUEL_VOLTAGE_MIN          1.0f        // 1V = 4mA
#define FUEL_VOLTAGE_MAX          5.0f        // 5V = 20mA

/* LED Pins - STM32F407G-DISC1 */
#define LED_POWER_PORT            GPIOD
#define LED_POWER_PIN             GPIO_PIN_12  // Green
#define LED_ACTIVITY_PORT         GPIOD
#define LED_ACTIVITY_PIN          GPIO_PIN_13  // Orange
#define LED_ERROR_PORT            GPIOD
#define LED_ERROR_PIN             GPIO_PIN_14  // Red
#define LED_GPS_PORT              GPIOD
#define LED_GPS_PIN               GPIO_PIN_15  // Blue

/* Button - User Button for Wake */
#define USER_BUTTON_PORT          GPIOA
#define USER_BUTTON_PIN           GPIO_PIN_0

/* Timing Configuration */
#define SENSOR_READ_INTERVAL_MS   5000        // 5 seconds
#define STOP_MODE_TIMEOUT_MS      30000       // 30 seconds
#define WATCHDOG_TIMEOUT_MS       10000       // 10 seconds

/* State Machine Configuration */
#define MAX_ERROR_COUNT           5
#define ERROR_RECOVERY_DELAY_MS   1000

/* Power Management */
#define RTC_WAKEup_PERIOD_MS      5000        // Wake every 5 seconds in STOP mode

/* Internal Log Configuration */
#define LOG_BUFFER_SIZE           16          // Number of logged entries

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H */
