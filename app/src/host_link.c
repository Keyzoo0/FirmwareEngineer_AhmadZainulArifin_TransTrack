/**
 * @file  host_link.c
 * @brief USART1: RX by circular DMA into a stream buffer, TX serialised with a mutex.
 */
#include "host_link.h"
#include "app_config.h"
#include "bsp.h"
#include "mpu_config.h"
#include "power_manager.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "stream_buffer.h"

static uint8_t s_dma_buf[HOST_RX_DMA_BUF_SIZE];     /* SRAM (DMA-reachable), not CCM */
static uint16_t s_last_pos;
static StreamBufferHandle_t s_rx;
static SemaphoreHandle_t s_tx_mutex;

void host_link_init(void)
{
    s_rx = xStreamBufferCreate(2048, 1);
    s_tx_mutex = xSemaphoreCreateMutex();
    host_link_rx_rearm();
}

void host_link_rx_rearm(void)
{
    s_last_pos = 0;
    (void)HAL_UARTEx_ReceiveToIdle_DMA(&huart1, s_dma_buf, sizeof(s_dma_buf));
}

int host_link_send(const void *data, size_t len)
{
    xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
    cache_clean(data, len);
    HAL_StatusTypeDef st = HAL_UART_Transmit(&huart1, (const uint8_t *)data, (uint16_t)len, 1000);
    xSemaphoreGive(s_tx_mutex);
    return st == HAL_OK ? 0 : -1;
}

size_t host_link_receive(uint8_t *buf, size_t max, uint32_t timeout_ms)
{
    return xStreamBufferReceive(s_rx, buf, max, pdMS_TO_TICKS(timeout_ms));
}

static void push(uint16_t from, uint16_t to, BaseType_t *woken)
{
    if (to > from) {
        cache_invalidate(&s_dma_buf[from], to - from);
        (void)xStreamBufferSendFromISR(s_rx, &s_dma_buf[from], to - from, woken);
    }
}

void host_link_on_rx_event(uint16_t pos)
{
    BaseType_t woken = pdFALSE;
    if (pos == s_last_pos) return;
    if (pos > s_last_pos) {
        push(s_last_pos, pos, &woken);
    } else {
        push(s_last_pos, HOST_RX_DMA_BUF_SIZE, &woken);
        push(0, pos, &woken);
    }
    s_last_pos = (pos == HOST_RX_DMA_BUF_SIZE) ? 0 : pos;
    power_note_activity();
    portYIELD_FROM_ISR(woken);
}
