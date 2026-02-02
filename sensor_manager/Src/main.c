/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program for STM32 Sensor Manager
 * @author        : Generated for STM32F407VGTx
 * @description   : Manages BME280, GPS, and Fuel Sensor with state machine
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software component, this software is provided AS-IS.
 *
 ******************************************************************************
 */

#include "main.h"
#include "state_machine.h"
#include "power_manager.h"
#include <stdio.h>

/* HAL Handles */
I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart1;  // Data output
UART_HandleTypeDef huart2;  // GPS
ADC_HandleTypeDef hadc1;
RTC_HandleTypeDef hrtc;

/* Global objects */
static state_machine_t g_state_machine;
static power_manager_t g_power_manager;

/* Private function prototypes */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_ADC1_Init(void);
static void MX_RTC_Init(void);
static void Error_Handler(void);

/**
 * @brief  The application entry point.
 */
int main(void)
{
    /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
    HAL_Init();

    /* Configure the system clock */
    SystemClock_Config();

    /* Initialize all configured peripherals */
    MX_GPIO_Init();
    MX_I2C1_Init();
    MX_USART1_UART_Init();
    MX_USART2_UART_Init();
    MX_ADC1_Init();
    MX_RTC_Init();

    /* Initialize FPU */
    #if __FPU_USED == 1
      SCB->CPACR |= ((3UL << 10*2) | (3UL << 11*2));  /* set CP10 and CP11 Full Access */
    #endif

    /* Initialize State Machine */
    if (state_machine_init(&g_state_machine) != 0) {
        Error_Handler();
    }

    /* Initialize Power Manager */
    if (power_manager_init(&g_power_manager) != 0) {
        Error_Handler();
    }

    /* Turn on power LED (Green) */
    HAL_GPIO_WritePin(LED_POWER_PORT, LED_POWER_PIN, GPIO_PIN_SET);

    /* Main application loop */
    while (1)
    {
        /* Run state machine */
        state_machine_run(&g_state_machine);

        /* Check if STOP mode should be entered */
        if (state_machine_should_enter_stop(&g_state_machine, STOP_MODE_TIMEOUT_MS)) {
            /* Enter STOP mode */
            power_manager_request_stop_mode(&g_power_manager);

            /* After wakeup, reinitialize peripherals that were stopped */
            MX_GPIO_Init();  /* Reinitialize GPIO states */
        }
    }
}

/**
 * @brief System Clock Configuration
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* Configure power supply */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /* Initialize HSE oscillator */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 8;  /* HSE 8MHz / 8 = 1MHz */
    RCC_OscInitStruct.PLL.PLLN = 336; /* 1MHz * 336 = 336MHz */
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2; /* 336MHz / 2 = 168MHz */
    RCC_OscInitStruct.PLL.PLLQ = 7;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /* Initialize CPU, AHB and APB buses clocks */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                  |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief I2C1 Initialization (for BME280)
 */
static void MX_I2C1_Init(void)
{
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief USART1 Initialization (Data Output)
 */
static void MX_USART1_UART_Init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = DATA_UART_BAUDRATE;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief USART2 Initialization (GPS)
 */
static void MX_USART2_UART_Init(void)
{
    huart2.Instance = USART2;
    huart2.Init.BaudRate = GPS_UART_BAUDRATE;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK)
    {
        Error_Handler();
    }

    /* Enable RX interrupt for GPS data */
    HAL_UART_Receive_IT(&huart2, &gps_rx_byte, 1);
}

/* GPS RX byte for interrupt */
static uint8_t gps_rx_byte = 0;

/**
 * @brief UART RX complete callback (GPS data)
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        /* Process GPS byte */
        gps_process_byte(&g_state_machine.context.gps, gps_rx_byte);

        /* Re-enable RX interrupt */
        HAL_UART_Receive_IT(&huart2, &gps_rx_byte, 1);
    }
}

/**
 * @brief ADC1 Initialization (Fuel Sensor)
 */
static void MX_ADC1_Init(void)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode = DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    if (HAL_ADC_Init(&hadc1) != HAL_OK)
    {
        Error_Handler();
    }

    /* Configure Channel 0 (PA0) for fuel sensor */
    sConfig.Channel = ADC_CHANNEL_0;
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_480CYCLES;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief RTC Initialization (for STOP mode wakeup)
 */
static void MX_RTC_Init(void)
{
    /* Initialize RTC */
    hrtc.Instance = RTC;
    hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
    hrtc.Init.AsynchPrediv = 127;
    hrtc.Init.SynchPrediv = 255;
    hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
    hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
    if (HAL_RTC_Init(&hrtc) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief GPIO Initialization
 */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* GPIO Ports Clock Enable */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* Configure LED pins as output */
    GPIO_InitStruct.Pin = LED_POWER_PIN | LED_ACTIVITY_PIN | LED_ERROR_PIN | LED_GPS_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    /* Turn off all LEDs initially (except power LED which is set in main) */
    HAL_GPIO_WritePin(GPIOD, LED_ACTIVITY_PIN | LED_ERROR_PIN | LED_GPS_PIN, GPIO_PIN_RESET);

    /* Configure User Button as input with interrupt */
    GPIO_InitStruct.Pin = USER_BUTTON_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* Enable and set EXTI interrupt for button */
    HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
}

/**
 * @brief  This function is executed in case of error occurrence.
 */
void Error_Handler(void)
{
    /* Disable interrupts */
    __disable_irq();

    /* Turn on error LED */
    HAL_GPIO_WritePin(LED_ERROR_PORT, LED_ERROR_PIN, GPIO_PIN_SET);

    /* Stay here */
    while (1)
    {
    }
}

/**
 * @brief EXTI line 0 interrupt (User Button)
 */
void EXTI0_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(USER_BUTTON_PIN);
}

#ifdef  USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 */
void assert_failed(uint8_t *file, uint32_t line)
{
    /* User can add his own implementation to report the file name and line number */
    printf("Wrong parameters value: file %s on line %lu\r\n", file, line);
}
#endif /* USE_FULL_ASSERT */
