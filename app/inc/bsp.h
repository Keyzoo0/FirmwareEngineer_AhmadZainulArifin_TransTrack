/**
 * @file  bsp.h
 * @brief Board support: clocks, GPIO, UARTs + DMA, I2C, ADC, RTC for STM32F407G-DISC1.
 */
#ifndef BSP_H
#define BSP_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>

extern I2C_HandleTypeDef  hi2c1;
extern UART_HandleTypeDef huart1;   /* host: telemetry + firmware update */
extern UART_HandleTypeDef huart2;   /* GPS */
extern ADC_HandleTypeDef  hadc1;
extern RTC_HandleTypeDef  hrtc;

void bsp_system_clock_config(void);   /* HSE 8 MHz -> PLL -> 168 MHz; also used after STOP */
void bsp_gpio_init(void);
void bsp_uart_init(void);
int  bsp_i2c_init(void);
void bsp_i2c_bus_recover(void);       /* 9 SCL clocks + STOP, then re-init */
void bsp_adc_init(void);
void bsp_rtc_init(void);

void bsp_led(uint16_t pin, bool on);
void bsp_led_toggle(uint16_t pin);

void bsp_iwdg_refresh(void);
uint32_t bsp_reset_cause_read_and_clear(void);   /* raw RCC_CSR flags */

#endif /* BSP_H */
