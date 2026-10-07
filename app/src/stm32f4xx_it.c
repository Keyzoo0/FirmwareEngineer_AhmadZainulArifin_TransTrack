/**
 * @file  stm32f4xx_it.c
 * @brief Interrupt handlers (SVC/PendSV/SysTick are mapped to FreeRTOS in FreeRTOSConfig.h).
 */
#include "bsp.h"

extern DMA_HandleTypeDef hdma_usart1_rx;
extern DMA_HandleTypeDef hdma_usart2_rx;

void NMI_Handler(void)
{
    for (;;) { }
}

void USART1_IRQHandler(void)       { HAL_UART_IRQHandler(&huart1); }
void USART2_IRQHandler(void)       { HAL_UART_IRQHandler(&huart2); }
void DMA2_Stream2_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_usart1_rx); }
void DMA1_Stream5_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_usart2_rx); }
void EXTI0_IRQHandler(void)        { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0); }
void RTC_WKUP_IRQHandler(void)     { HAL_RTCEx_WakeUpTimerIRQHandler(&hrtc); }
