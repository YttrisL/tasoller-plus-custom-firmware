/* System control: protected registers, resets, interrupts, start-up. */

#include "ss321.h"

/* System control block registers */
#define SCB_VTOR        (*(volatile uint32_t *)0xE000ED08u)       /* vector table address */
#define SCB_SHPR_BYTES  (*(volatile uint8_t *)0xE000ED14u)        /* priorities of the system handlers */
#define SCB_CPACR       (*(volatile uint32_t *)0xE000ED88u)       /* coprocessor access: the FPU */

void SystemInit(void)
{
  uint32_t value;
  
  value = SCB_CPACR;
  SCB_CPACR = value | 0xf00000;
  value = FMC_CYCCTL;
  FMC_CYCCTL = value & 0xfffffff0 | 8;
  value = CLK_LDOCTL;
  CLK_LDOCTL = value | 0x40000;
  value = CLK_APBCLK0;
  CLK_APBCLK0 = value | 2;
  value = RTC_GPIOCTL0;
  RTC_GPIOCTL0 = value & 0xf7f7f7f7;
  value = RTC_GPIOCTL1;
  RTC_GPIOCTL1 = value & 0xf7f7f7f7;
  value = CLK_APBCLK0;
  CLK_APBCLK0 = value & 0xfffffffd;
  value = GPIO_PF_MODE;
  GPIO_PF_MODE = value & 0xffffff0f;
  return;
}

void SYS_UnlockReg(void)
{
  int locked;
  
  do {
    SYS_REGLCTL = 0x59;
    SYS_REGLCTL = 0x16;
    SYS_REGLCTL = 0x88;
    locked = SYS_REGLCTL;
  } while (locked == 0);
  return;
}

void SYS_ResetModule(uint32_t module)
{
  volatile uint32_t *reset_reg;
  
  reset_reg = (volatile uint32_t *)(&SYS_IPRST0 + (module >> 0x18));
  *reset_reg = *reset_reg | 1 << (module & 0xff);
  *reset_reg = *reset_reg & ~(1 << (module & 0xff));
  return;
}

void NVIC_EnableIRQ(uint32_t irq)
{
  *(int *)(&NVIC_ISER0 + (irq >> 5) * 4) = 1 << (irq & 0x1f);
  return;
}

void NVIC_DisableIRQ(uint32_t irq)
{
  *(int *)(&NVIC_ICER0 + (irq >> 5) * 4) = 1 << (irq & 0x1f);
  return;
}

void NVIC_SetPriority(uint32_t irq,uint32_t priority)
{
  if ((int)irq < 0) {
    (&SCB_SHPR_BYTES)[irq & 0xf] = (uint8_t)((priority & 0xf) << 4);
  }
  else {
    (&NVIC_IPR0)[irq] = (uint8_t)((priority & 0xf) << 4);
  }
  return;
}

void SCB_SetVectorTable(uint32_t address)
{
  SCB_VTOR = address;
  return;
}

void NVIC_SystemReset(void)
{
  uint32_t aircr;
  
  DataSynchronizationBarrier(0xf);
  aircr = SCS_AIRCR;
  SCS_AIRCR = (aircr & 0x700 | 0x5fa0000) + 4;
  DataSynchronizationBarrier(0xf);
  for (;;) {
  }
}
