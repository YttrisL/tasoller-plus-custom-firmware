/* The board: clocks, pins, the millisecond tick, and the information that the firmware reads from the
 * flash when it starts. */

#include "ss321.h"

/* ---- Clocks and pins ----------------------------------------------------------------------- */

/* Clock module ids in the encoding of the Nuvoton clock library (the first two bits say which clock
 * enable register, the low five bits which bit of it, the rest where the clock source is chosen). */
#define MODULE_PDMA         1               /* AHBCLK bit 1 */
#define MODULE_USBD         0x42803c9bu     /* APBCLK0 bit 27 */
#define MODULE_TMR0         0x5e800002u     /* APBCLK0 bit 2 */
#define MODULE_TMR1         0x5ec00003u     /* APBCLK0 bit 3 */
#define MODULE_SPI0         0x6620000cu     /* APBCLK0 bit 12 */
#define MODULE_SPI1         0x6640000du     /* APBCLK0 bit 13 */
#define MODULE_SPI2         0x6660000eu     /* APBCLK0 bit 14 */
#define MODULE_UART1        0x57a03d91u     /* APBCLK0 bit 17 */
#define MODULE_UART2        0x778c3c12u
#define MODULE_UART3        0x77ac3c93u
#define MODULE_UART4        0x77cc3d14u
#define MODULE_UART5        0x77ec3d95u

/* Clock sources, as the value of the selection field in its position in the register */
#define UART1_SRC       0x4000000u
#define UART2_SRC       0x1000000u
#define UART3_SRC       0x4000000u
#define UART4_SRC       0x10000000u
#define UART5_SRC       0x1000000u
#define TMR0_SRC        0
#define TMR1_SRC      0x2000u
#define SPI0_SRC        0x4u
#define SPI1_SRC        0x10u
#define SPI2_SRC        0x40u

#define CORE_CLOCK_HZ       192000000
#define APPLICATION_ADDRESS 0x4000          /* the vector table, behind the bootloader */

/* Start the crystal, run the core at 192 MHz from the PLL, set up the USB clock and the
 * clocks of the peripherals in use, and connect the pins. */
void board_clock_and_pin_init(void)
{
    SYS_UnlockReg();
    SCB_SetVectorTable(APPLICATION_ADDRESS);
    GPIO_PF_MODE &= 0xffffff0f;                 /* PF2, PF3: the crystal pins are not GPIOs */
    CLK_EnableXtalRC(1);
    CLK_WaitClockReady(1);
    CLK_SetHCLK(0, 0);
    CLK_SetCoreClock(CORE_CLOCK_HZ);
    CLK_PCLKDIV = 0x11;                         /* PCLK0 and PCLK1 at HCLK / 2 */
    SYS_USBPHY = (SYS_USBPHY & ~3u) | 0x104;    /* USB in device mode, PHY on */
    CLK_CLKDIV0 = (CLK_CLKDIV0 & 0xffffff0f) | 0x30;    /* USB clock divider */
    CLK_EnableModuleClock(MODULE_USBD);

    CLK_SetModuleClock(MODULE_UART5, UART5_SRC, 0);
    CLK_SetModuleClock(MODULE_UART1, UART1_SRC, 0);
    CLK_SetModuleClock(MODULE_UART2, UART2_SRC, 0);
    CLK_SetModuleClock(MODULE_UART3, UART3_SRC, 0);
    CLK_SetModuleClock(MODULE_UART4, UART4_SRC, 0);
    CLK_EnableModuleClock(MODULE_UART1);
    CLK_SetModuleClock(MODULE_TMR0, TMR0_SRC, 0);
    CLK_EnableModuleClock(MODULE_PDMA);
    CLK_EnableModuleClock(MODULE_TMR0);
    CLK_SetModuleClock(MODULE_TMR1, TMR1_SRC, 0);
    CLK_EnableModuleClock(MODULE_TMR1);
    CLK_EnableModuleClock(MODULE_SPI1);
    CLK_SetModuleClock(MODULE_SPI1, SPI1_SRC, 0);
    CLK_EnableModuleClock(MODULE_SPI2);
    CLK_SetModuleClock(MODULE_SPI2, SPI2_SRC, 0);
    CLK_EnableModuleClock(MODULE_SPI0);
    CLK_SetModuleClock(MODULE_SPI0, SPI0_SRC, 0);

    /* Pin functions (a nibble per pin) */
    GPIO_PA_MODE &= 0xc0ffffff;                 /* PA12 to PA15 are not GPIOs */
    SYS_GPA_MFPH &= 0xffff;
    SYS_GPA_MFPH |= 0xeeee0000;                 /* PA12 to PA15: the USB pins */
    SYS_GPA_MFPH &= 0xffffff00;
    SYS_GPA_MFPH |= 0x77;                       /* PA8, PA9: the serial port of UART1 */
    SYS_GPA_MFPL &= 0xfffffff0;
    SYS_GPA_MFPL |= 3;                          /* PA0: the data pin of tower A's QSPI0 */
    GPIO_SetSlewCtl(&GPIO_PA_MODE, 1, 1);
    SYS_GPF_MFPL &= 0xff00ffff;
    SYS_GPF_MFPL |= 0x220000;                   /* PF4, PF5: UART2 */
    SYS_GPC_MFPL &= 0xffff00ff;
    SYS_GPC_MFPL |= 0xbb00;                     /* PC2, PC3: UART3 */
    SYS_GPC_MFPL &= 0xf0ffffff;
    SYS_GPC_MFPL |= 0x4000000;                  /* PC6: the data pin of tower B's SPI1 */
    GPIO_SetSlewCtl(&GPIO_PC_MODE, 0x40, 1);
    SYS_GPA_MFPL &= 0xffff00ff;
    SYS_GPA_MFPL |= 0x7700;                     /* PA2, PA3: UART4 */
    SYS_GPA_MFPH &= 0xfffff0ff;
    SYS_GPA_MFPL &= 0xff00ffff;
    SYS_GPA_MFPL |= 0x880000;                   /* PA4, PA5: UART5 */
    SYS_GPC_MFPL &= 0xff0fffff;
    SYS_GPC_MFPL = SYS_GPC_MFPL;
    SYS_GPC_MFPH &= 0xf0ffffff;
    SYS_GPC_MFPH = SYS_GPC_MFPH;
    SYS_REGLCTL = 0;
}

#define GPIO_MODE_OUTPUT    1
#define GPIO_MODE_QUASI     3
#define GPIO_PULL_UP        1
#define GPIO_INT_BOTH_EDGES 0x10001u
#define GPC_IRQ             0x12
#define GPD_IRQ             0x13

/* Configure the pins: the air emitters, the receivers and the buttons (with an interrupt on both
 * edges), and the output pins that the rest of the firmware drives. */
void board_gpio_init(void)
{
    uint32_t i;

    for (i = 0; i < INPUT_PINS; i = (i + 1) & 0xff) {
        GPIO_SetMode((volatile uint32_t *)input_pins[i].port, input_pins[i].mask, GPIO_MODE_QUASI);
        GPIO_SetPullCtl((volatile uint32_t *)input_pins[i].port, input_pins[i].mask, GPIO_PULL_UP);
        if (i < AIR_BEAMS) {
            GPIO_SetPullCtl((volatile uint32_t *)air_led_pins[i].port, air_led_pins[i].mask, GPIO_PULL_UP);
            GPIO_SetMode((volatile uint32_t *)air_led_pins[i].port, air_led_pins[i].mask, GPIO_MODE_OUTPUT);
        }
    }
    GPIO_EnableInt(input_pins[INPUT_PIN_SERVICE_BUTTON].port, input_pins[INPUT_PIN_SERVICE_BUTTON].number, GPIO_INT_BOTH_EDGES);
    GPIO_EnableInt(input_pins[INPUT_PIN_TEST_BUTTON].port, input_pins[INPUT_PIN_TEST_BUTTON].number, GPIO_INT_BOTH_EDGES);
    NVIC_EnableIRQ(GPC_IRQ);
    NVIC_EnableIRQ(GPD_IRQ);
    GPIO_PB0_PDIO = 1;
    GPIO_PB1_PDIO = 1;
    GPIO_PB2_PDIO = 1;
    GPIO_PB3_PDIO = 1;
    GPIO_PB4_PDIO = 1;
    GPIO_PB5_PDIO = 1;
    GPIO_SetMode(&GPIO_PC_MODE, 0x4000, GPIO_MODE_OUTPUT);
    GPIO_PC14_PDIO = 1;
    GPIO_SetMode(&GPIO_PC_MODE, 0x10, GPIO_MODE_OUTPUT);
    GPIO_PC4_PDIO = 1;
    GPIO_SetMode(&GPIO_PC_MODE, 0x20, GPIO_MODE_OUTPUT);
    GPIO_PC5_PDIO = 0;                          /* PC5 holds the touch controllers in reset */
    GPIO_SetMode(&GPIO_PB_MODE, 0x40, GPIO_MODE_QUASI);
    GPIO_SetPullCtl(&GPIO_PB_MODE, 0x40, 0);
}

/* ---- The tick ---------------------------------------------------------------------------------- */

#define TICK_PRIORITY       0x30
#define TICK_FREQUENCY      1000
#define TIMER_PERIODIC      0x8000000u
#define TIMER_INT_ENABLE    0x20000000u
#define TIMER_ENABLE        0x40000000u
#define TMR0_IRQ_ENABLE_BIT 1                   /* bit 0 of ISER1: IRQ 32 */

/* Timer 0 counts the milliseconds in tick_ms. */
void tick_timer_start(void)
{
    NVIC_IPR8 = TICK_PRIORITY;
    TIMER_Open(&TMR01_TIMER0_CTL, TIMER_PERIODIC, TICK_FREQUENCY);
    TMR01_TIMER0_CTL |= TIMER_INT_ENABLE;
    TMR01_TIMER0_CTL |= TIMER_ENABLE;
    NVIC_ISER1 = TMR0_IRQ_ENABLE_BIT;
}

/* Timer 0 interrupt: count a millisecond. */
void TMR0_IRQHandler(void)
{
    TMR01_TIMER0_INTSTS = 1;
    tick_ack = 0;
    tick_ms = tick_ms + 1;
    if (settings_save_delay_ms != 0 && (settings_save_delay_ms = settings_save_delay_ms - 1, settings_save_delay_ms == 0)) {
        settings_dirty = 1;
    }
}

/* ---- Information from the flash --------------------------------------------------------------- */

#define HARDWARE_INFO_FLASH     0x7f000
#define HARDWARE_INFO_WORDS     0x20
#define DEFAULT_HARDWARE_INFO   "SS321-V02"

/* Read the hardware information (64 bytes, a string) from flash 0x7f000. A blank block is first filled
 * with the default "SS321-V02". */
void hardware_info_load(void)
{
    uint32_t block[HARDWARE_INFO_WORDS];
    uint32_t i;

    SYS_UnlockReg();
    FMC_Open();
    FMC_ISPCTL |= 8;
    for (i = 0; i < HARDWARE_INFO_WORDS; i = (i + 1) & 0xffff) {
        block[i] = FMC_Read(HARDWARE_INFO_FLASH + i * 4);
    }
    if (block[0] == 0xffffffff) {
        memcpy(block, DEFAULT_HARDWARE_INFO, 9);
        for (i = 9; i < 18; i++) {              /* the rest of the first 18 bytes is cleared */
            ((uint8_t *)block)[i] = 0;
        }
        for (i = 0; i < HARDWARE_INFO_WORDS; i = (i + 1) & 0xffff) {
            FMC_Write(HARDWARE_INFO_FLASH + i * 4, block[i]);
        }
        for (i = 0; i < HARDWARE_INFO_WORDS; i = (i + 1) & 0xffff) {
            block[i] = FMC_Read(HARDWARE_INFO_FLASH + i * 4);
        }
    }
    memcpy((void *)hardware_info, block, 0x40);
    FMC_Close();
    SYS_REGLCTL = 0;
}

#define BOOTLOADER_AREA_SIZE    0x4000
#define BOOTLOADER_STEP         0x1000
#define BOOTLOADER_WINDOW       0x2000

/* Look for the version string of the bootloader in the first 16 KiB of flash and keep it and what follows
 * (64 bytes). Nothing reads it afterwards. */
void bootloader_version_scan(void)
{
    uint32_t base, offset;

    SYS_UnlockReg();
    FMC_Open();
    for (base = 0; base < BOOTLOADER_AREA_SIZE; base += BOOTLOADER_STEP) {
        FMC_ReadRange(base, base + BOOTLOADER_WINDOW, (uint32_t *)flash_buffer);
        for (offset = 0; offset < 0x1ff2; offset++) {
            if (memcmp((const uint8_t *)&flash_buffer[offset], BOOTLOADER_TAG, sizeof(BOOTLOADER_TAG) - 1) == 0) {
                memcpy((void *)bootloader_info, (const void *)&flash_buffer[offset], 0x40);
                bootloader_info[0x3f] = 0;
                FMC_Close();
                SYS_REGLCTL = 0;
                return;
            }
        }
    }
    FMC_Close();
    SYS_REGLCTL = 0;
}

/* The sum of `size` bytes of flash from `address`, 4 KiB at a time. */
uint8_t flash_checksum(int address, uint32_t size)
{
    uint8_t sum = 0;
    uint32_t done;

    for (done = 0; done < size; done += 0x1000) {
        FMC_ReadRange(address + done, address + done + 0x1000, (uint32_t *)flash_buffer);
        if (size - done < 0x1000) {
            sum += checksum8((const uint8_t *)flash_buffer, size - done);
        } else {
            sum += checksum8((const uint8_t *)flash_buffer, 0x1000);
        }
    }
    return sum;
}
