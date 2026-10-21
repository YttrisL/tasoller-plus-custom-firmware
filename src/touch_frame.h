#pragma once
/* What touch_frame.c defines. */

void touch_parse_frame(uint32_t port);
uint32_t touch_detect_boot_banner(uint32_t port);
void touch_uart_task(void);
