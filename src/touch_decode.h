#pragma once
/* What touch_decode.c defines. */

uint32_t winusb_decode_touch(const uint8_t *frame, int chip);
uint32_t keyboard_decode_touch(const uint8_t *frame, int chip);
