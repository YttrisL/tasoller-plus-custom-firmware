/* GPIO pins (the names and the behaviour of the Nuvoton library). */

#include "ss321.h"

void GPIO_SetMode(volatile uint32_t *port,uint32_t pin_mask,int mode)
{
  uint32_t pin;
  
  for (pin = 0; pin < 0x10; pin = pin + 1) {
    if ((1 << (pin & 0xff) & pin_mask) == 1 << (pin & 0xff)) {
      *port = *port & ~(3 << ((pin & 0x7f) << 1)) | mode << ((pin & 0x7f) << 1);
    }
  }
  return;
}

void GPIO_SetPullCtl(int port,uint32_t pin_mask,int mode)
{
  uint32_t pin;
  
  for (pin = 0; pin < 0x10; pin = pin + 1) {
    if ((1 << (pin & 0xff) & pin_mask) != 0) {
      *(volatile uint32_t *)(port + 0x30) =
           *(volatile uint32_t *)(port + 0x30) & ~(3 << ((pin & 0x7f) << 1)) |
           mode << ((pin & 0x7f) << 1);
    }
  }
  return;
}

void GPIO_SetSlewCtl(int port,uint32_t pin_mask,int mode)
{
  uint32_t pin;
  
  for (pin = 0; pin < 0x10; pin = pin + 1) {
    if ((1 << (pin & 0xff) & pin_mask) != 0) {
      *(volatile uint32_t *)(port + 0x28) =
           *(volatile uint32_t *)(port + 0x28) & ~(3 << ((pin & 0x7f) << 1)) |
           mode << ((pin & 0x7f) << 1);
    }
  }
  return;
}

void GPIO_EnableInt(int port,uint32_t pin,uint32_t int_attribs)
{
  *(volatile uint32_t *)(port + 0x18) =
       *(volatile uint32_t *)(port + 0x18) & ~(1 << (pin & 0xff)) | (int_attribs >> 0x18) << (pin & 0xff);
  *(volatile uint32_t *)(port + 0x1c) =
       *(volatile uint32_t *)(port + 0x1c) & ~(0x10001 << (pin & 0xff)) |
       (int_attribs & 0xffffff) << (pin & 0xff);
  return;
}
