#pragma once
/* What settings.c defines. */

void settings_flush(int force);
void key_map_reset_4k(void);
void key_map_reset_8k(void);
void key_map_reset_16k(void);
void key_map_reset_32k(void);
void thresholds_reset(void);
void settings_load(void);
bool settings_replace(const volatile uint8_t *block);
