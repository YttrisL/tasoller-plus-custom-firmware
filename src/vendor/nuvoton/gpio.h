#pragma once
/* What gpio.c defines. */

void GPIO_SetMode(volatile uint32_t *port,uint32_t pin_mask,int mode);
void GPIO_SetPullCtl(int port,uint32_t pin_mask,int mode);
void GPIO_SetSlewCtl(int port,uint32_t pin_mask,int mode);
void GPIO_EnableInt(int port,uint32_t pin,uint32_t int_attribs);
