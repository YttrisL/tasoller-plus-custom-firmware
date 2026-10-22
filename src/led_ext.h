#pragma once
/* What led_ext.c defines. */

bool led_ext_packet(const uint8_t *data, uint32_t length);
bool led_ext_active(void);
void led_ext_load(int tower);
uint32_t led_ext_command(const uint8_t *request);
void led_ext_towers(uint32_t tower, const uint8_t *colors);
