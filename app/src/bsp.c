/**
 * @file  bsp.c
 * @brief Board support for STM32F407G-DISC1.
 *
 * Interrupt priorities (NVIC group 4, lower number = more urgent):
 *   5        = configMAX_SYSCALL_INTERRUPT_PRIORITY, highest level allowed to call FreeRTOS
 *   6        USART1/USART2 + their DMA streams (call FromISR APIs)
 *   7        EXTI0 (button), RTC wake-up
 *   15       TIM6 HAL timebase, SysTick/PendSV (kernel)
 */
#include "bsp.h"
#include "app_config.h"

I2C_HandleTypeDef  hi2c1;
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
ADC_HandleTypeDef  hadc1;
RTC_HandleTypeDef  hrtc;
DMA_HandleTypeDef  hdma_usart1_rx;
DMA_HandleTypeDef  hdma_usart2_rx;

void bsp_system_clock_config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_LSI;
    osc.HSEState = RCC_HSE_ON;
    osc.LSIState = RCC_LSI_ON;                  /* RTC clock: DISC1 has no LSE crystal fitted */
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 8;
    osc.PLL.PLLN = 336;
    osc.PLL.PLLP = RCC_PLLP_DIV2;               /* 168 MHz */
    osc.PLL.PLLQ = 7;                           /* 48 MHz */
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        NVIC_SystemReset();
    }
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;         /* 42 MHz */
    clk.APB2CLKDivider = RCC_HCLK_DIV2;         /* 84 MHz */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) {
        NVIC_SystemReset();
    }
}

void bsp_gpio_init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    HAL_GPIO_WritePin(LED_PORT, LED_POWER_PIN | LED_ACTIVITY_PIN | LED_ERROR_PIN | LED_GPS_PIN, GPIO_PIN_RESET);
    g.Pin = LED_POWER_PIN | LED_ACTIVITY_PIN | LED_ERROR_PIN | LED_GPS_PIN;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &g);

    /* User button: external pull-down on the board, rising edge = press, also wakes from STOP */
    g.Pin = BUTTON_PIN;
    g.Mode = GPIO_MODE_IT_RISING;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(BUTTON_PORT, &g);
    HAL_NVIC_SetPriority(EXTI0_IRQn, 7, 0);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
}

static void uart_dma_rx_init(DMA_HandleTypeDef *hdma, DMA_Stream_TypeDef *stream, UART_HandleTypeDef *huart,
                             IRQn_Type irq)
{
    hdma->Instance = stream;
    hdma->Init.Channel = DMA_CHANNEL_4;
    hdma->Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma->Init.PeriphInc = DMA_PINC_DISABLE;
    hdma->Init.MemInc = DMA_MINC_ENABLE;
    hdma->Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma->Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma->Init.Mode = DMA_CIRCULAR;
    hdma->Init.Priority = DMA_PRIORITY_MEDIUM;
    hdma->Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(hdma) != HAL_OK) {
        NVIC_SystemReset();
    }
    __HAL_LINKDMA(huart, hdmarx, *hdma);
    HAL_NVIC_SetPriority(irq, 6, 0);
    HAL_NVIC_EnableIRQ(irq);
}

static void uart_init_one(UART_HandleTypeDef *h, USART_TypeDef *inst, uint32_t baud)
{
    h->Instance = inst;
    h->Init.BaudRate = baud;
    h->Init.WordLength = UART_WORDLENGTH_8B;
    h->Init.StopBits = UART_STOPBITS_1;
    h->Init.Parity = UART_PARITY_NONE;
    h->Init.Mode = UART_MODE_TX_RX;
    h->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    h->Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(h) != HAL_OK) {
        NVIC_SystemReset();
    }
}

void bsp_uart_init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();

    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Pin = GPIO_PIN_9 | GPIO_PIN_10;           /* USART1 TX/RX */
    g.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_2 | GPIO_PIN_3;            /* USART2 TX/RX */
    g.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &g);

    uart_init_one(&huart1, USART1, HOST_BAUDRATE);
    uart_init_one(&huart2, USART2, GPS_BAUDRATE);

    /* RM0090 table 43/44: USART1_RX = DMA2 Stream2 Ch4, USART2_RX = DMA1 Stream5 Ch4 */
    uart_dma_rx_init(&hdma_usart1_rx, DMA2_Stream2, &huart1, DMA2_Stream2_IRQn);
    uart_dma_rx_init(&hdma_usart2_rx, DMA1_Stream5, &huart2, DMA1_Stream5_IRQn);

    HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    HAL_NVIC_SetPriority(USART2_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}

int bsp_i2c_init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_I2C1_CLK_ENABLE();
    g.Pin = GPIO_PIN_6 | GPIO_PIN_7;            /* SCL, SDA */
    g.Mode = GPIO_MODE_AF_OD;
    g.Pull = GPIO_NOPULL;                       /* external 4.7k pull-ups, see docs/HARDWARE.md */
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &g);

    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    return (HAL_I2C_Init(&hi2c1) == HAL_OK) ? 0 : -1;
}

/*
 * A slave that was interrupted mid-byte (reset, brown-out, noise) can hold SDA low forever,
 * which makes every HAL transfer fail with BUSY. Standard recovery (NXP UM10204 3.1.16):
 * clock SCL up to 9 times until SDA is released, then generate a STOP condition.
 */
void bsp_i2c_bus_recover(void)
{
    GPIO_InitTypeDef g = {0};
    HAL_I2C_DeInit(&hi2c1);

    g.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    g.Mode = GPIO_MODE_OUTPUT_OD;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &g);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_SET);

    for (int i = 0; i < 9 && HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7) == GPIO_PIN_RESET; i++) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
        for (volatile int d = 0; d < 400; d++) { }   /* ~5 us half period */
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
        for (volatile int d = 0; d < 400; d++) { }
    }
    /* STOP: SDA low -> high while SCL high */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);
    for (volatile int d = 0; d < 400; d++) { }
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);

    /* Software-reset the peripheral to clear a stuck BUSY flag (ES0182 2.5.7 errata) */
    __HAL_RCC_I2C1_FORCE_RESET();
    __HAL_RCC_I2C1_RELEASE_RESET();
    (void)bsp_i2c_init();
}

void bsp_adc_init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_ADC1_CLK_ENABLE();
    g.Pin = GPIO_PIN_1;                         /* PA1 = ADC1_IN1 (PA0 is the user button) */
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;   /* 21 MHz (max 36) */
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
    if (HAL_ADC_Init(&hadc1) != HAL_OK) {
        NVIC_SystemReset();
    }
}

void bsp_rtc_init(void)
{
    RCC_PeriphCLKInitTypeDef pclk = {0};
    HAL_PWR_EnableBkUpAccess();
    pclk.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    pclk.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
    (void)HAL_RCCEx_PeriphCLKConfig(&pclk);
    __HAL_RCC_RTC_ENABLE();

    hrtc.Instance = RTC;
    hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
    hrtc.Init.AsynchPrediv = 127;
    hrtc.Init.SynchPrediv = 249;                /* LSI 32 kHz -> 1 Hz */
    hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
    hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
    (void)HAL_RTC_Init(&hrtc);

    HAL_NVIC_SetPriority(RTC_WKUP_IRQn, 7, 0);
    HAL_NVIC_EnableIRQ(RTC_WKUP_IRQn);
}

void bsp_led(uint16_t pin, bool on)
{
    HAL_GPIO_WritePin(LED_PORT, pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void bsp_led_toggle(uint16_t pin)
{
    HAL_GPIO_TogglePin(LED_PORT, pin);
}

void bsp_iwdg_refresh(void)
{
    IWDG->KR = 0xAAAAU;    /* IWDG was started by the bootloader with an 8 s timeout */
}

uint32_t bsp_reset_cause_read_and_clear(void)
{
    uint32_t csr = RCC->CSR;
    RCC->CSR |= RCC_CSR_RMVF;
    return csr;
}
