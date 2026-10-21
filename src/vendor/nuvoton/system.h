#pragma once
/* What system.c defines. */

void SystemInit(void);
void SYS_UnlockReg(void);
void SYS_ResetModule(uint32_t module);
void NVIC_EnableIRQ(uint32_t irq);
void NVIC_DisableIRQ(uint32_t irq);
void NVIC_SetPriority(uint32_t irq,uint32_t priority);
void SCB_SetVectorTable(uint32_t address);
void NVIC_SystemReset(void);
