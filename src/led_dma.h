#pragma once
/* What led_dma.c defines. */

void ws2812_encode(const volatile rgb_t *rgb, int strip);
void led_tower_a_dma_start(void);
void led_tower_b_dma_start(void);
void led_aux_dma_start(void);
void led_slider_dma_start(void);
void led_dma_task(void);
void led_hardware_init(void);
