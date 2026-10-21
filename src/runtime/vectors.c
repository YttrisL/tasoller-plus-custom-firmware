/* Interrupt vector table, 110 entries. */

#include "ss321.h"

typedef void (*vector_t)(void);

extern void BOD_IRQHandler(void);
extern void BusFault_Handler(void);
extern void DebugMon_Handler(void);
extern void GPA_IRQHandler(void);
extern void GPB_IRQHandler(void);
extern void GPC_IRQHandler(void);
extern void GPD_IRQHandler(void);
extern void GPF_IRQHandler(void);
extern void HardFault_Handler(void);
extern void MemManage_Handler(void);
extern void NMI_Handler(void);
extern void PendSV_Handler(void);
extern void Reset_Handler(void);
extern void SVC_Handler(void);
extern void SysTick_Handler(void);
extern void TMR0_IRQHandler(void);
extern void UART2_IRQHandler(void);
extern void UART3_IRQHandler(void);
extern void UART4_IRQHandler(void);
extern void UART5_IRQHandler(void);
extern void USBD_IRQHandler(void);
extern void UsageFault_Handler(void);

__attribute__((section(".vectors"), used))
const uint32_t g_vectors[110] = {
    0x2001ed48,   /* initial stack pointer */
    (uint32_t)Reset_Handler,   /* 1 */
    (uint32_t)NMI_Handler,   /* 2 */
    (uint32_t)HardFault_Handler,   /* 3 */
    (uint32_t)MemManage_Handler,   /* 4 */
    (uint32_t)BusFault_Handler,   /* 5 */
    (uint32_t)UsageFault_Handler,   /* 6 */
    0,   /* 7 */
    0,   /* 8 */
    0,   /* 9 */
    0,   /* 10 */
    (uint32_t)SVC_Handler,   /* 11 */
    (uint32_t)DebugMon_Handler,   /* 12 */
    0,   /* 13 */
    (uint32_t)PendSV_Handler,   /* 14 */
    (uint32_t)SysTick_Handler,   /* 15 */
    (uint32_t)BOD_IRQHandler,   /* 16 */
    (uint32_t)BOD_IRQHandler,   /* 17 */
    (uint32_t)BOD_IRQHandler,   /* 18 */
    (uint32_t)BOD_IRQHandler,   /* 19 */
    (uint32_t)BOD_IRQHandler,   /* 20 */
    (uint32_t)BOD_IRQHandler,   /* 21 */
    (uint32_t)BOD_IRQHandler,   /* 22 */
    (uint32_t)BOD_IRQHandler,   /* 23 */
    (uint32_t)BOD_IRQHandler,   /* 24 */
    (uint32_t)BOD_IRQHandler,   /* 25 */
    (uint32_t)BOD_IRQHandler,   /* 26 */
    (uint32_t)BOD_IRQHandler,   /* 27 */
    (uint32_t)BOD_IRQHandler,   /* 28 */
    (uint32_t)BOD_IRQHandler,   /* 29 */
    (uint32_t)BOD_IRQHandler,   /* 30 */
    (uint32_t)BOD_IRQHandler,   /* 31 */
    (uint32_t)GPA_IRQHandler,   /* 32 */
    (uint32_t)GPB_IRQHandler,   /* 33 */
    (uint32_t)GPC_IRQHandler,   /* 34 */
    (uint32_t)GPD_IRQHandler,   /* 35 */
    (uint32_t)BOD_IRQHandler,   /* 36 */
    (uint32_t)GPF_IRQHandler,   /* 37 */
    (uint32_t)BOD_IRQHandler,   /* 38 */
    (uint32_t)BOD_IRQHandler,   /* 39 */
    (uint32_t)BOD_IRQHandler,   /* 40 */
    (uint32_t)BOD_IRQHandler,   /* 41 */
    (uint32_t)BOD_IRQHandler,   /* 42 */
    (uint32_t)BOD_IRQHandler,   /* 43 */
    (uint32_t)BOD_IRQHandler,   /* 44 */
    (uint32_t)BOD_IRQHandler,   /* 45 */
    (uint32_t)BOD_IRQHandler,   /* 46 */
    (uint32_t)BOD_IRQHandler,   /* 47 */
    (uint32_t)TMR0_IRQHandler,   /* 48 */
    (uint32_t)BOD_IRQHandler,   /* 49 */
    (uint32_t)BOD_IRQHandler,   /* 50 */
    (uint32_t)BOD_IRQHandler,   /* 51 */
    (uint32_t)BOD_IRQHandler,   /* 52 */
    (uint32_t)BOD_IRQHandler,   /* 53 */
    (uint32_t)BOD_IRQHandler,   /* 54 */
    (uint32_t)BOD_IRQHandler,   /* 55 */
    (uint32_t)BOD_IRQHandler,   /* 56 */
    (uint32_t)BOD_IRQHandler,   /* 57 */
    (uint32_t)BOD_IRQHandler,   /* 58 */
    (uint32_t)BOD_IRQHandler,   /* 59 */
    (uint32_t)BOD_IRQHandler,   /* 60 */
    (uint32_t)BOD_IRQHandler,   /* 61 */
    (uint32_t)BOD_IRQHandler,   /* 62 */
    (uint32_t)BOD_IRQHandler,   /* 63 */
    (uint32_t)UART2_IRQHandler,   /* 64 */
    (uint32_t)UART3_IRQHandler,   /* 65 */
    (uint32_t)BOD_IRQHandler,   /* 66 */
    (uint32_t)BOD_IRQHandler,   /* 67 */
    (uint32_t)BOD_IRQHandler,   /* 68 */
    (uint32_t)USBD_IRQHandler,   /* 69 */
    (uint32_t)BOD_IRQHandler,   /* 70 */
    (uint32_t)BOD_IRQHandler,   /* 71 */
    (uint32_t)BOD_IRQHandler,   /* 72 */
    (uint32_t)BOD_IRQHandler,   /* 73 */
    (uint32_t)BOD_IRQHandler,   /* 74 */
    (uint32_t)BOD_IRQHandler,   /* 75 */
    (uint32_t)BOD_IRQHandler,   /* 76 */
    (uint32_t)BOD_IRQHandler,   /* 77 */
    (uint32_t)BOD_IRQHandler,   /* 78 */
    (uint32_t)BOD_IRQHandler,   /* 79 */
    (uint32_t)BOD_IRQHandler,   /* 80 */
    (uint32_t)BOD_IRQHandler,   /* 81 */
    (uint32_t)BOD_IRQHandler,   /* 82 */
    (uint32_t)BOD_IRQHandler,   /* 83 */
    (uint32_t)BOD_IRQHandler,   /* 84 */
    (uint32_t)BOD_IRQHandler,   /* 85 */
    (uint32_t)BOD_IRQHandler,   /* 86 */
    (uint32_t)BOD_IRQHandler,   /* 87 */
    (uint32_t)BOD_IRQHandler,   /* 88 */
    (uint32_t)BOD_IRQHandler,   /* 89 */
    (uint32_t)UART4_IRQHandler,   /* 90 */
    (uint32_t)UART5_IRQHandler,   /* 91 */
    (uint32_t)BOD_IRQHandler,   /* 92 */
    (uint32_t)BOD_IRQHandler,   /* 93 */
    (uint32_t)BOD_IRQHandler,   /* 94 */
    (uint32_t)BOD_IRQHandler,   /* 95 */
    (uint32_t)BOD_IRQHandler,   /* 96 */
    (uint32_t)BOD_IRQHandler,   /* 97 */
    (uint32_t)BOD_IRQHandler,   /* 98 */
    (uint32_t)BOD_IRQHandler,   /* 99 */
    (uint32_t)BOD_IRQHandler,   /* 100 */
    (uint32_t)BOD_IRQHandler,   /* 101 */
    (uint32_t)BOD_IRQHandler,   /* 102 */
    (uint32_t)BOD_IRQHandler,   /* 103 */
    (uint32_t)BOD_IRQHandler,   /* 104 */
    (uint32_t)BOD_IRQHandler,   /* 105 */
    (uint32_t)BOD_IRQHandler,   /* 106 */
    (uint32_t)BOD_IRQHandler,   /* 107 */
    (uint32_t)BOD_IRQHandler,   /* 108 */
    (uint32_t)BOD_IRQHandler,   /* 109 */
};
