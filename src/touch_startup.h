#pragma once
/* What touch_startup.c defines. */

void delay_ms(uint32_t ms);
bool wait_for_flag(uint32_t timeout, volatile uint8_t *flag, uint32_t expected);
uint32_t wait_for_touch_boot(uint32_t ports);
void touch_startup_task(void);
