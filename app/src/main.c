/**
 * @file  main.c (application)
 * @brief Sensor Manager application: started by the bootloader from slot A or B.
 */
#include "app_config.h"
#include "boot_control.h"
#include "bsp.h"
#include "event_log.h"
#include "gps.h"
#include "host_link.h"
#include "mpu_config.h"
#include "power_manager.h"
#include "sys_monitor.h"
#include "tasks.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

/* FreeRTOS heap in CCM RAM (see FreeRTOSConfig.h) */
uint8_t ucHeap[configTOTAL_HEAP_SIZE] __attribute__((section(".ccmram"), aligned(8)));

void fault_log_flush(void);   /* fault_handler.c */

static void banner(uint32_t csr)
{
    char b[160];
    int n = snprintf(b, sizeof(b),
                     "\r\n{\"event\":\"boot\",\"fw\":\"%d.%d.%d\",\"slot\":\"%c\",\"trial\":%s,\"rcc_csr\":\"0x%08lx\"}\r\n",
                     FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH, 'A' + APP_SLOT,
                     boot_control_is_trial() ? "true" : "false", (unsigned long)csr);
    (void)host_link_send(b, (size_t)n);
}

int main(void)
{
    /* VTOR was set by the bootloader and again by SystemInit (USER_VECT_TAB_ADDRESS) */
    HAL_Init();
    bsp_system_clock_config();
    mpu_config();
    bsp_iwdg_refresh();

    bsp_gpio_init();
    bsp_uart_init();
    (void)bsp_i2c_init();
    bsp_adc_init();
    bsp_rtc_init();
    bsp_led(LED_POWER_PIN, true);

    uint32_t csr = bsp_reset_cause_read_and_clear();
    event_log_init();
    boot_control_init();
    fault_log_flush();        /* a fault captured in RTC backup registers before the reset */
    event_log_write(EVT_BOOT, (uint16_t)(csr >> 24),
                    (FW_VERSION_MAJOR << 16) | (FW_VERSION_MINOR << 8) | FW_VERSION_PATCH);

    host_link_init();
    power_init();
    banner(csr);

    BaseType_t ok = pdPASS;
    ok &= xTaskCreate(sys_monitor_task, "monitor",   256, NULL, 4, NULL);
    ok &= xTaskCreate(update_task,      "update",    512, NULL, 3, NULL);
    ok &= xTaskCreate(telemetry_task,   "telemetry", 512, NULL, 2, NULL);
    if (ok != pdPASS) {
        NVIC_SystemReset();
    }
    vTaskStartScheduler();
    NVIC_SystemReset();       /* only reached if the idle task could not be created */
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t pos)
{
    if (huart->Instance == USART2) {
        gps_on_rx_event(pos);
    } else if (huart->Instance == USART1) {
        host_link_on_rx_event(pos);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    /* Overrun/noise/framing: HAL aborts the DMA transfer; re-arm the reception */
    if (huart->Instance == USART2) {
        gps_rx_rearm();
    } else if (huart->Instance == USART1) {
        host_link_rx_rearm();
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    if (pin == BUTTON_PIN) {
        power_note_activity();
    }
}
