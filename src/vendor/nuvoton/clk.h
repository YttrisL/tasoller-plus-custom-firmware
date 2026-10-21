#pragma once
/* What clk.c defines. */

void CLK_EnableXtalRC(uint32_t mask);
void CLK_DisablePLL(void);
void CLK_EnableModuleClock(uint32_t module);
uint32_t CLK_WaitClockReady(uint32_t mask);
int32_t CLK_GetPLLClockFreq(void);
void SystemCoreClockUpdate(void);
uint32_t CLK_GetHCLKFreq(void);
uint32_t CLK_GetPCLK0Freq(void);
uint32_t CLK_GetPCLK1Freq(void);
void CLK_SetHCLK(uint32_t source, uint32_t divider);
void CLK_SetModuleClock(uint32_t module, uint32_t sel, uint32_t div);
int32_t CLK_EnablePLL(uint32_t source, uint32_t pll_freq);
uint32_t CLK_SetCoreClock(uint32_t hz);
