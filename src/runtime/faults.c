/* Fault and unused interrupt handlers: they stop the program, the hard fault prints the registers. */

#include "ss321.h"

void NMI_Handler(void)
{
  for (;;) {
  }
}

void MemManage_Handler(void)
{
  for (;;) {
  }
}

void BusFault_Handler(void)
{
  for (;;) {
  }
}

void UsageFault_Handler(void)
{
  for (;;) {
  }
}

void SVC_Handler(void)
{
  for (;;) {
  }
}

void DebugMon_Handler(void)
{
  for (;;) {
  }
}

void PendSV_Handler(void)
{
  for (;;) {
  }
}

void SysTick_Handler(void)
{
  for (;;) {
  }
}

void BOD_IRQHandler(void)
{
  for (;;) {
  }
}

/* Pass the stack pointer that was in use when the fault happened (MSP or PSP, bit 2 of the
 * exception return value in lr) and lr itself to ProcessHardFault, which prints the registers. */
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "movs r0, #4\n"
        "mov r1, lr\n"
        "tst r0, r1\n"
        "beq 1f\n"
        "mrs r0, psp\n"
        "b 2f\n"
        "1: mrs r0, msp\n"
        "2: mov r1, lr\n"
        "ldr r2, =ProcessHardFault\n"
        "bx r2\n"
        ".ltorg\n");
}

void ProcessHardFault(uint32_t frame)
{
  print_fault_frame(frame);
  for (;;) {
  }
}

void print_fault_frame(uint32_t *frame)
{
  debug_printf("r0  = 0x%x\n",*frame);
  debug_printf("r1  = 0x%x\n",frame[1]);
  debug_printf("r2  = 0x%x\n",frame[2]);
  debug_printf("r3  = 0x%x\n",frame[3]);
  debug_printf("r12 = 0x%x\n",frame[4]);
  debug_printf("lr  = 0x%x\n",frame[5]);
  debug_printf("pc  = 0x%x\n",frame[6]);
  debug_printf("psr = 0x%x\n",frame[7]);
  return;
}
