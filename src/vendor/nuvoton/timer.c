/* Timers (Nuvoton library). */

#include "ss321.h"

uint32_t TIMER_GetModuleClock(volatile uint32_t *timer_ctl)
{
  uint32_t clock_hz;
  uint32_t clock_select;
  uint32_t source_hz [8];
  
  memcpy(source_hz,&timer_clock_sources,0x20);
  if (timer_ctl == &TMR01_TIMER0_CTL) {
    clock_select = CLK_CLKSEL1;
    clock_select = (clock_select & 0x7ff) >> 8;
  }
  else if (timer_ctl == &TMR01_TIMER1_CTL) {
    clock_select = CLK_CLKSEL1;
    clock_select = (clock_select & 0x7fff) >> 0xc;
  }
  else if (timer_ctl == (uint32_t *)&TMR23_TIMER2_CTL) {
    clock_select = CLK_CLKSEL1;
    clock_select = (clock_select & 0x7ffff) >> 0x10;
  }
  else {
    clock_select = CLK_CLKSEL1;
    clock_select = (clock_select & 0x7fffff) >> 0x14;
  }
  if (clock_select == 2) {
    if ((timer_ctl == &TMR01_TIMER0_CTL) || (timer_ctl == &TMR01_TIMER1_CTL)) {
      clock_hz = CLK_GetPCLK0Freq();
    }
    else {
      clock_hz = CLK_GetPCLK1Freq();
    }
  }
  else {
    clock_hz = source_hz[clock_select];
  }
  return clock_hz;
}

uint32_t TIMER_Open(volatile uint32_t *timer,uint32_t mode,uint32_t freq)
{
  uint32_t clock;
  uint32_t prescale;
  
  clock = TIMER_GetModuleClock(timer);
  prescale = 0;
  if (clock >> 1 < freq) {
    freq = 2;
  }
  else {
    freq = clock / freq;
    prescale = freq >> 0x18;
    if (prescale != 0) {
      freq = freq / (prescale + 1);
    }
  }
  *timer = mode | prescale;
  timer[1] = freq;
  return clock / (freq * (prescale + 1));
}

void TIMER_SetTriggerSource(int timer,uint32_t source)
{
  *(volatile uint32_t *)(timer + 0x1c) = *(volatile uint32_t *)(timer + 0x1c) & 0xfffffffe | source;
  return;
}

void TIMER_SetTriggerTarget(int timer,uint32_t target)
{
  *(volatile uint32_t *)(timer + 0x1c) = *(volatile uint32_t *)(timer + 0x1c) & 0xffffffe1 | target;
  return;
}

/* table read by TIMER_GetModuleClock */
const uint8_t timer_clock_sources[32] = {
    0x00, 0x1b, 0xb7, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1b, 0xb7, 0x00,
};
