#pragma once
/* What stream.c defines. */

void stream_control_task(void);
void stream_queue_push(const volatile uint8_t *packet, int flags);
uint32_t stream_send(const volatile uint8_t *data, uint32_t size);
void stream_queue_pop(void);
void touch_report_task(void);
