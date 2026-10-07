/**
 * @file  app_config.h
 * @brief Application configuration (STM32F407G-DISC1 + BME280 + GPS + 4-20 mA fuel sender).
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* Firmware version, overridable from CMake (-DFW_VERSION_MAJOR=...) */
#ifndef FW_VERSION_MAJOR
#define FW_VERSION_MAJOR          1
#endif
#ifndef FW_VERSION_MINOR
#define FW_VERSION_MINOR          1
#endif
#ifndef FW_VERSION_PATCH
#define FW_VERSION_PATCH          0
#endif
#ifndef APP_SLOT                              /* 0 = slot A, 1 = slot B, set by CMake per image */
#define APP_SLOT                  0
#endif

/* ---- BME280 on I2C1: PB6 = SCL, PB7 = SDA (100 kHz, external 4.7k pull-ups) ---- */
#define BME280_I2C_ADDR           0x76U       /* SDO tied to GND */
#define BME280_I2C_TIMEOUT_MS     100U        /* every transfer is bounded (Level 1 fix) */
#define BME280_MAX_RETRIES        3U

/* ---- GPS on USART2: PA2 = TX, PA3 = RX, 9600 8N1, NMEA 0183 ---- */
#define GPS_BAUDRATE              9600U
#define GPS_DMA_BUF_SIZE          256U        /* circular DMA, drained on IDLE / half / full */
#define GPS_FIX_TIMEOUT_MS        5000U       /* fix considered stale after this */

/* ---- Telemetry + firmware update on USART1: PA9 = TX, PA10 = RX, 115200 8N1 ---- */
#define HOST_BAUDRATE             115200U
#define HOST_RX_DMA_BUF_SIZE      512U

/* ---- Fuel sender: 4-20 mA through 250R shunt -> 10k/10k divider -> PA1 (ADC1_IN1) ----
 * PA0 is the user button on the Discovery board, so the original PA0 assignment was moved. */
#define FUEL_SHUNT_OHMS           250.0f
#define FUEL_DIVIDER_RATIO        0.5f        /* 20 mA = 5.0 V -> 2.5 V; 24 mA still < 3.0 V VDDA */
#define FUEL_OVERSAMPLE           16U
#define FUEL_AVG_WINDOW           8U          /* moving average over READ cycles */

/* ---- Status LEDs (active high) ---- */
#define LED_POWER_PIN             GPIO_PIN_12 /* green  : alive / heartbeat */
#define LED_ACTIVITY_PIN          GPIO_PIN_13 /* orange : sensor read / update in progress */
#define LED_ERROR_PIN             GPIO_PIN_14 /* red    : ERROR state */
#define LED_GPS_PIN               GPIO_PIN_15 /* blue   : valid GPS fix */
#define LED_PORT                  GPIOD

/* ---- User button (wake / activity) ---- */
#define BUTTON_PIN                GPIO_PIN_0
#define BUTTON_PORT               GPIOA

/* ---- Timing ---- */
#define SENSOR_READ_INTERVAL_MS   5000U
#define STOP_MODE_IDLE_MS         30000U      /* no activity and no GPS fix for 30 s -> STOP */
#define RTC_WAKEUP_PERIOD_S       5U          /* in STOP: wake every 5 s to sample (< IWDG 8 s) */
#define IWDG_TIMEOUT_MS           8000U       /* started by the bootloader, cannot be stopped */
#define BOOT_CONFIRM_MS           10000U      /* new image must be healthy this long before confirming */
#define HEARTBEAT_TIMEOUT_MS      6000U       /* task considered hung if silent this long */

/* ---- Error handling ---- */
#define MAX_ERROR_COUNT           5U          /* consecutive failed cycles before ERROR state */
#define MAX_RECOVERY_ATTEMPTS     5U          /* ERROR -> re-init attempts before a controlled reset */

#endif /* APP_CONFIG_H */
