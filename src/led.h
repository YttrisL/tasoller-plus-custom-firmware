#pragma once
/* What led.c defines. */

void hsv_to_rgb(const volatile hsv_t *hsv, int count, volatile rgb_t *rgb);
int32_t rgb_to_hsv(const volatile rgb_t *rgb, int count, volatile hsv_t *hsv);
void slider_fill(float hue, float saturation, float value);
void slider_off(void);
void slider_clear(void);
void slider_rainbow(void);
bool slider_fade_in_step(void);
bool slider_fade_out_step(void);
void slider_progress(uint32_t percent);
uint32_t winusb_slider_key_led(float level, int key);
void keyboard_slider_key_led(float level, int key);
void air_led_set(int beam, uint32_t blocked);
void led_load_packet(int section);
void led_hsv_to_rgb_task(void);
void led_brightness_task(void);
