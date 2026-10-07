/**
 * @file  gps.h
 * @brief GPS receiver on USART2: circular DMA + IDLE-line reception, NMEA RMC decoding.
 */
#ifndef GPS_H
#define GPS_H

#include "nmea.h"
#include <stdbool.h>

typedef struct {
    nmea_rmc_t rmc;
    bool       fix;            /* valid RMC received within GPS_FIX_TIMEOUT_MS */
    uint32_t   age_ms;
    uint32_t   checksum_errors;
    uint32_t   sentences;
} gps_status_t;

void gps_start(void);                  /* (re)arm DMA reception, also after STOP mode */
void gps_rx_rearm(void);               /* after a UART error (ISR-safe) */
void gps_get(gps_status_t *out);
void gps_on_rx_event(uint16_t dma_pos); /* called from HAL_UARTEx_RxEventCallback (ISR) */

#endif /* GPS_H */
