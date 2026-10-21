#pragma once
/* What personality_winusb.c defines. */

void dbt_set_pad_value(int index, uint16_t value);
uint32_t winusb_air_beam(uint32_t beam, int blocked);
uint32_t winusb_service_button(int unused, int pressed);
uint32_t winusb_test_button(int unused, int pressed);
uint8_t * winusb_report_in(uint32_t unused, uint16_t *length);
void winusb_report_out(uint8_t *data, uint32_t length);
void winusb_personality_init(void);
