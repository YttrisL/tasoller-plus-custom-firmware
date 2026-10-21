#pragma once
/* What timer.c defines. */

uint32_t TIMER_GetModuleClock(volatile uint32_t *timer_ctl);
uint32_t TIMER_Open(volatile uint32_t *timer,uint32_t mode,uint32_t freq);
void TIMER_SetTriggerSource(int timer,uint32_t source);
void TIMER_SetTriggerTarget(int timer,uint32_t target);
