/**
 * @file  host_link.h
 * @brief USART1 shared by telemetry (JSON lines out) and the update protocol (binary frames).
 */
#ifndef HOST_LINK_H
#define HOST_LINK_H

#include <stddef.h>
#include <stdint.h>

void   host_link_init(void);
int    host_link_send(const void *data, size_t len);          /* mutex-protected, blocking */
size_t host_link_receive(uint8_t *buf, size_t max, uint32_t timeout_ms);
void   host_link_on_rx_event(uint16_t dma_pos);                /* ISR */
void   host_link_rx_rearm(void);                               /* after a UART error (ISR-safe) */

#endif /* HOST_LINK_H */
