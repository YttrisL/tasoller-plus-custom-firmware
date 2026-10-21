#pragma once
/* What board.c defines. */

void board_clock_and_pin_init(void);
void board_gpio_init(void);
void tick_timer_start(void);
void TMR0_IRQHandler(void);
void hardware_info_load(void);
void bootloader_version_scan(void);
uint8_t flash_checksum(int address, uint32_t size);
