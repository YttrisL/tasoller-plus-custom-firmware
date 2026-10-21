#pragma once
/* What touch_uart.c defines. */

void touch_send_all(const uint8_t *data, int length);
void touch_send_to(int port, const uint8_t *data, int length);
void touch_rx_flush(void);
void touch_uart_init(uint32_t baud);
void bridge_mode_enter(uint32_t baud);
void bridge_mode_exit(void);
