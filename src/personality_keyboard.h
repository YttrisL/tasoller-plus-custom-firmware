#pragma once
/* What personality_keyboard.c defines. */

uint8_t keyboard_pad_key(int write, uint8_t value, int index);
uint8_t keyboard_air_key(int write, int blocked, int beam);
uint8_t keyboard_service_button(int write, int pressed);
uint8_t keyboard_test_button(int write, int pressed);
uint8_t * keyboard_report_in(uint32_t unused, uint16_t *length);
void keyboard_report_out(void);
void keyboard_personality_init(void);
