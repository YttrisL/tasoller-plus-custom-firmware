/* UARTs (Nuvoton library), and the debug console on UART1. */

#include "ss321.h"

uint64_t UART_Open(volatile uint32_t *uart,uint32_t baud_rate)
{
  uint32_t clock_source;
  uint32_t divisor;
  uint32_t clock_divider;
  uint32_t clock_sources [4];
  
  clock_source = 0;
  clock_divider = 0;
  clock_sources[0] = 12000000;
  clock_sources[1] = 0;
  clock_sources[2] = 0x8000;
  clock_sources[3] = 12000000;
  if (uart == (uint32_t *)&UART0_DAT) {
    clock_source = CLK_CLKSEL1;
    clock_source = (clock_source & 0x3ffffff) >> 0x18;
    clock_divider = CLK_CLKDIV0;
    clock_divider = (clock_divider & 0xfff) >> 8;
  }
  else if (uart == &UART1_DAT) {
    clock_source = CLK_CLKSEL1;
    clock_source = (clock_source & 0xfffffff) >> 0x1a;
    clock_divider = CLK_CLKDIV0;
    clock_divider = (clock_divider & 0xffff) >> 0xc;
  }
  else if (uart == &UART2_DAT) {
    clock_source = CLK_CLKSEL3;
    clock_source = (clock_source & 0x3ffffff) >> 0x18;
    clock_divider = CLK_CLKDIV4;
    clock_divider = clock_divider & 0xf;
  }
  else if (uart == &UART3_DAT) {
    clock_source = CLK_CLKSEL3;
    clock_source = (clock_source & 0xfffffff) >> 0x1a;
    clock_divider = CLK_CLKDIV4;
    clock_divider = (clock_divider & 0xff) >> 4;
  }
  else if (uart == &UART4_DAT) {
    clock_source = CLK_CLKSEL3;
    clock_source = (clock_source & 0x3fffffff) >> 0x1c;
    clock_divider = CLK_CLKDIV4;
    clock_divider = (clock_divider & 0xfff) >> 8;
  }
  else if (uart == &UART5_DAT) {
    clock_source = CLK_CLKSEL3;
    clock_source = clock_source >> 0x1e;
    clock_divider = CLK_CLKDIV4;
    clock_divider = (clock_divider & 0xffff) >> 0xc;
  }
  uart[0xc] = 0;
  uart[3] = 3;
  uart[2] = uart[2] & 0xfff0ff0f;
  if (clock_source == 1) {
    clock_sources[1] = CLK_GetPLLClockFreq();
  }
  if (baud_rate != 0) {
    divisor = (clock_sources[clock_source] / (clock_divider + 1) + (baud_rate >> 1)) / baud_rate - 2;
    if (divisor < 0x10000) {
      uart[9] = divisor | 0x30000000;
    }
    else {
      uart[9] = ((clock_sources[clock_source] / (clock_divider + 1) + baud_rate * 8) / baud_rate >> 4) - 2;
    }
  }
  return CONCAT44(clock_sources[1],clock_sources[0]);
}

void UART_EnableInt(int uart,uint32_t int_mask)
{
  *(volatile uint32_t *)(uart + 4) = *(volatile uint32_t *)(uart + 4) | int_mask;
  return;
}

void UART_DisableInt(int uart,uint32_t int_mask)
{
  *(volatile uint32_t *)(uart + 4) = *(volatile uint32_t *)(uart + 4) & ~int_mask;
  return;
}

void UART_ClearIntFlag(int uart,uint32_t flags)
{
  if ((flags & 0x400) != 0) {
    *(volatile uint32_t *)(uart + 0x18) = 0x70;
    *(volatile uint32_t *)(uart + 0x18) = 8;
  }
  if ((flags & 0x800) != 0) {
    *(volatile uint32_t *)(uart + 0x14) = *(volatile uint32_t *)(uart + 0x14) | 1;
  }
  if ((flags & 0x2000) != 0) {
    *(volatile uint32_t *)(uart + 0x18) = 0x1000001;
  }
  if ((flags & 0x4000) != 0) {
    *(volatile uint32_t *)(uart + 0x44) = 0x1f;
  }
  if ((flags & 0x8000) != 0) {
    *(volatile uint32_t *)(uart + 0x1c) = 0x80;
    *(volatile uint32_t *)(uart + 0x38) = 0x30f;
  }
  return;
}

/* Send len bytes on a UART, waiting for the transmit FIFO to be empty before each byte (UART
 * FIFO status register, bit 28). Gives up silently when it never empties. */
void UART_Write(volatile uint32_t *uart, const uint8_t *data, int len)
{
    int i;

    for (i = 0; i != len; i++) {
        uint32_t spins = 0;

        while (!(uart[6] & 0x10000000)) {
            if (++spins >= 0x40000000) {
                return;
            }
        }
        uart[0] = data[i];
    }
}

void debug_uart_putc(int c)
{
  uint32_t fifo_status;
  
  do {
    fifo_status = UART1_FIFOSTS;
  } while ((fifo_status & 0x800000) != 0);
  UART1_DAT = c;
  if (c == 10) {
    do {
      fifo_status = UART1_FIFOSTS;
    } while ((fifo_status & 0x800000) != 0);
    UART1_DAT = 0xd;
  }
  return;
}

void debug_putchar(uint32_t c)
{
  debug_uart_putc(c);
  return;
}
