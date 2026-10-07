/**
 * @file  gps.c
 * @brief GPS reception with circular DMA and IDLE-line detection.
 *
 * Changes compared with the February version: it received one byte per interrupt
 * (HAL_UART_Receive_IT re-armed in the callback) and parsed the sentence with double math
 * inside that ISR. A single overrun error stopped reception for good, because nothing re-armed
 * the UART in HAL_UART_ErrorCallback. Now the DMA fills a circular buffer continuously; on IDLE,
 * half-transfer and transfer-complete events only the new bytes are fed to the parser, and a
 * UART error re-arms the reception (main.c). Parsing a byte is O(1), so ISR time stays bounded.
 */
#include "gps.h"
#include "bsp.h"
#include "app_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

/* DMA target must be in SRAM1/2 (CCM RAM is not reachable by DMA) */
static uint8_t s_dma_buf[GPS_DMA_BUF_SIZE];
static uint16_t s_last_pos;
static nmea_parser_t s_parser;
static nmea_rmc_t s_last_rmc;
static volatile uint32_t s_last_fix_tick;
static volatile bool s_have_fix;

void gps_rx_rearm(void)
{
    s_last_pos = 0;
    __HAL_UART_CLEAR_OREFLAG(&huart2);
    (void)HAL_UARTEx_ReceiveToIdle_DMA(&huart2, s_dma_buf, sizeof(s_dma_buf));
}

void gps_start(void)
{
    nmea_init(&s_parser);
    (void)HAL_UART_AbortReceive(&huart2);    /* task context only: polls the DMA abort */
    gps_rx_rearm();
}

static void feed_range(uint16_t from, uint16_t to)
{
    nmea_rmc_t rmc;
    for (uint16_t i = from; i < to; i++) {
        if (nmea_feed(&s_parser, (char)s_dma_buf[i], &rmc) == NMEA_RMC) {
            s_last_rmc = rmc;
            if (rmc.valid) {
                s_have_fix = true;
                s_last_fix_tick = xTaskGetTickCountFromISR();
            }
        }
    }
}

void gps_on_rx_event(uint16_t pos)
{
    if (pos == s_last_pos) return;
    if (pos > s_last_pos) {
        feed_range(s_last_pos, pos);
    } else {                                   /* wrapped around */
        feed_range(s_last_pos, GPS_DMA_BUF_SIZE);
        feed_range(0, pos);
    }
    s_last_pos = (pos == GPS_DMA_BUF_SIZE) ? 0 : pos;
}

void gps_get(gps_status_t *out)
{
    taskENTER_CRITICAL();
    out->rmc = s_last_rmc;
    uint32_t age = (xTaskGetTickCount() - s_last_fix_tick) * portTICK_PERIOD_MS;
    out->fix = s_have_fix && s_last_rmc.valid && age < GPS_FIX_TIMEOUT_MS;
    out->age_ms = s_have_fix ? age : UINT32_MAX;
    out->checksum_errors = s_parser.checksum_errors;
    out->sentences = s_parser.sentences_ok;
    taskEXIT_CRITICAL();
}
