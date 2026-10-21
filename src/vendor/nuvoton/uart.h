#pragma once
/* What uart.c defines. */

uint64_t UART_Open(volatile uint32_t *uart,uint32_t baud_rate);
void UART_EnableInt(int uart,uint32_t int_mask);
void UART_DisableInt(int uart,uint32_t int_mask);
void UART_ClearIntFlag(int uart,uint32_t flags);
void UART_Write(volatile uint32_t *uart, const uint8_t *data, int len);
void debug_uart_putc(int c);
void debug_putchar(uint32_t c);
