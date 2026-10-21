#pragma once
/* What command.c defines. */

uint32_t command_process(uint8_t *request);
void command_task(void);
void command_send_reply(int code, const uint8_t *data, int length);
