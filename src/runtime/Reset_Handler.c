// Reset_Handler @ 0x41e8
void Reset_Handler(void)

{
  uint32_t value;

  boot_snapshot_take();
  SYS_REGLCTL = 0x59;
  SYS_REGLCTL = 0x16;
  SYS_REGLCTL = 0x88;
  value = CLK_AHBCLK;
  CLK_AHBCLK = value | 0x4000;
  value = SPIM_CTL1;
  SPIM_CTL1 = value | 2;
  value = SPIM_CTL1;
  SPIM_CTL1 = value | 4;
  SystemInit();
  SYS_REGLCTL = 0;
  fw_init_data();
  main();
  return;
}
