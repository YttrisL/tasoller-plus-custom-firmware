#pragma once
/* What faults.c defines. */

void NMI_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);
void BOD_IRQHandler(void);
void ProcessHardFault(uint32_t frame);
void print_fault_frame(uint32_t *frame);
