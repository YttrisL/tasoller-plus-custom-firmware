/* Clock controller helpers (the same job as Nuvoton's CLK driver for the M480). */

#include "ss321.h"

#define CLK_PWRCTL_HXTEN   0x01u
#define CLK_PWRCTL_HIRCEN  0x04u
#define CLK_PLLCTL_PD      0x10000u
#define CLK_STATUS_HIRCSTB 0x10u
#define CLK_STATUS_PLLSTB  0x04u
#define PLL_SOURCE_HIRC    0x80000u

/* Switch the 12 MHz crystal / HIRC on. */
void CLK_EnableXtalRC(uint32_t mask)
{
    CLK_PWRCTL |= mask;
}

/* Power the PLL down. */
void CLK_DisablePLL(void)
{
    CLK_PLLCTL |= CLK_PLLCTL_PD;
}

/* Enable the clock of a module. The module number holds the register index in bits 31..30
 * (0 AHBCLK, 1 APBCLK0, 2 APBCLK1) and the bit position in bits 4..0. */
void CLK_EnableModuleClock(uint32_t module)
{
    (&CLK_AHBCLK)[module >> 30] |= 1u << (module & 0x1f);
}

/* Wait until every clock in mask reports stable. Returns 1, or 0 after a time-out, in which
 * case g_clk_timeout_flag is set to 0xffffffff (it is cleared on entry). */
uint32_t CLK_WaitClockReady(uint32_t mask)
{
    uint32_t timeout = SystemCoreClock >> 1;
    uint32_t stable = 1;

    g_clk_timeout_flag = 0;
    do {
        if ((CLK_STATUS & mask) == mask) {
            goto done;
        }
        timeout--;
    } while (timeout != 0);
    stable = 0;
done:
    if (timeout == 0) {
        g_clk_timeout_flag = 0xffffffff;
    }
    return stable;
}

/* Frequency of the PLL in Hz, 0 when it is powered down or bypassed. */
int32_t CLK_GetPLLClockFreq(void)
{
    static const uint8_t outdiv_table[4] = { 1, 2, 2, 4 };
    int32_t freq = 0;
    uint32_t ctl = CLK_PLLCTL;

    if ((ctl & 0x50000) == 0) {                     /* neither PD nor OE disabled */
        if ((ctl & 0x20000) == 0x20000) {           /* bypass: the input clock */
            freq = 12000000;
        } else {
            uint32_t fbdiv = (ctl & 0x1ff) + 2;
            int16_t indiv = (int16_t)(((ctl << 18) >> 27) + 1);
            int16_t outdiv = (int16_t)outdiv_table[(ctl & 0xffff) >> 14];

            freq = (int32_t)((fbdiv * 3000000u) / (uint32_t)((int32_t)indiv * (int32_t)outdiv)) << 3;
        }
    }
    return freq;
}

/* Recompute SystemCoreClock and CyclesPerUs from the clock registers. */
void SystemCoreClockUpdate(void)
{
    uint32_t source;
    uint32_t sel;

    PllClock = (uint32_t)CLK_GetPLLClockFreq();
    sel = CLK_CLKSEL0;
    source = PllClock;
    if ((sel & 7) != 2) {
        source = gau32ClkSrcTbl[sel & 7];
    }
    SystemCoreClock = source / ((CLK_CLKDIV0 & 0xf) + 1);
    CyclesPerUs = (SystemCoreClock + 500000) / 1000000;
}

/* HCLK in Hz. */
uint32_t CLK_GetHCLKFreq(void)
{
    SystemCoreClockUpdate();
    return SystemCoreClock;
}

/* PCLK0 in Hz (HCLK divided by 1, 2, 4, 8 or 16). */
uint32_t CLK_GetPCLK0Freq(void)
{
    uint32_t freq;

    SystemCoreClockUpdate();
    freq = SystemCoreClock;
    if ((CLK_PCLKDIV & 7) != 0) {
        if ((CLK_PCLKDIV & 7) == 1) {
            freq = SystemCoreClock >> 1;
        } else if ((CLK_PCLKDIV & 7) == 2) {
            freq = SystemCoreClock >> 2;
        } else if ((CLK_PCLKDIV & 7) == 3) {
            freq = SystemCoreClock >> 3;
        } else if ((CLK_PCLKDIV & 7) == 4) {
            freq = SystemCoreClock >> 4;
        }
    }
    return freq;
}

/* PCLK1 in Hz. */
uint32_t CLK_GetPCLK1Freq(void)
{
    uint32_t freq;

    SystemCoreClockUpdate();
    freq = SystemCoreClock;
    if ((CLK_PCLKDIV & 0x70) != 0) {
        if ((CLK_PCLKDIV & 0x70) == 0x10) {
            freq = SystemCoreClock >> 1;
        } else if ((CLK_PCLKDIV & 0x70) == 0x20) {
            freq = SystemCoreClock >> 2;
        } else if ((CLK_PCLKDIV & 0x70) == 0x30) {
            freq = SystemCoreClock >> 3;
        } else if ((CLK_PCLKDIV & 0x70) == 0x40) {
            freq = SystemCoreClock >> 4;
        }
    }
    return freq;
}

/* Select the HCLK source and divider. Returns after the new setting is in use. */
void CLK_SetHCLK(uint32_t source, uint32_t divider)
{
    uint32_t status = CLK_STATUS;

    CLK_PWRCTL |= CLK_PWRCTL_HIRCEN;
    CLK_WaitClockReady(CLK_STATUS_HIRCSTB);
    /* go through HIRC while the divider changes */
    CLK_CLKSEL0 = (CLK_CLKSEL0 & ~7u) + 7;
    CLK_CLKDIV0 = (CLK_CLKDIV0 & ~0xfu) | divider;
    CLK_CLKSEL0 = (CLK_CLKSEL0 & ~7u) | source;
    SystemCoreClockUpdate();
    if ((status & CLK_STATUS_HIRCSTB) == 0) {
        CLK_PWRCTL &= ~CLK_PWRCTL_HIRCEN;
    }
}

/* Set the clock of a module: `module` encodes the divider and selector fields (see the
 * M480 CLK_SetModuleClock), `sel` is the selector value and `div` the divider value. */
void CLK_SetModuleClock(uint32_t module, uint32_t sel, uint32_t div)
{
    volatile uint32_t *reg;

    if (((module & 0x3ffff) >> 10) != 0) {          /* the module has a divider */
        uint32_t index = (module & 0xfffff) >> 18;

        if ((SYS_CSERVER & 0xff) == 1) {
            if (index == 2 && (module & 0x1f) == 0x1f) {
                reg = &CLK_CLKDIV2;
            } else if (index == 2 && (module & 0x1f) == 0x1d) {
                reg = &CLK_CLKDIV2;
            } else if (index == 2) {
                reg = &CLK_CLKDIV3;
            } else if (index == 3) {
                reg = &CLK_CLKDIV4;
            } else {
                reg = &CLK_CLKDIV0 + index;
            }
        } else if (index == 2) {
            reg = &CLK_CLKDIV3;
        } else if (index == 3) {
            reg = &CLK_CLKDIV4;
        } else {
            reg = &CLK_CLKDIV0 + index;
        }
        *reg = (*reg & ~(((module & 0x3ffff) >> 10) << ((module & 0x3ff) >> 5))) | div;
    }
    if (((module & 0xfffffff) >> 25) != 0) {        /* the module has a clock selector */
        volatile uint32_t *sel_reg = &CLK_CLKSEL0 + ((module & 0x3fffffff) >> 28);

        *sel_reg = (*sel_reg & ~(((module & 0xfffffff) >> 25) << ((module & 0x1ffffff) >> 20))) | sel;
    }
}

/* Start the PLL from the crystal (source 0) or from HIRC (anything else) at about pll_freq Hz
 * and wait for it. Returns the resulting frequency. */
int32_t CLK_EnablePLL(uint32_t source, uint32_t pll_freq)
{
    uint32_t best_error;
    uint32_t best_outdiv, best_indiv, best_fbdiv;
    uint32_t src_bit;
    int32_t result;

    CLK_DisablePLL();
    if (source == 0) {
        CLK_PWRCTL |= CLK_PWRCTL_HXTEN;
        CLK_WaitClockReady(1);
        src_bit = 0;
    } else {
        CLK_PWRCTL |= CLK_PWRCTL_HIRCEN;
        CLK_WaitClockReady(CLK_STATUS_HIRCSTB);
        src_bit = PLL_SOURCE_HIRC;
    }

    if (pll_freq <= 0x1dcd6500 && pll_freq >= 50000000) {
        uint32_t error = 0xffffffff;
        uint32_t outdiv_sel, indiv, fbdiv;
        uint32_t target = pll_freq;

        best_outdiv = 0;
        best_indiv = 0;
        best_fbdiv = 0;
        for (outdiv_sel = 1; outdiv_sel < 5 && error != 0; outdiv_sel++) {
            if (outdiv_sel == 3) {
                continue;
            }
            if (outdiv_sel == 4) {
                target = pll_freq << 2;
            } else if (outdiv_sel == 2) {
                target = pll_freq << 1;
            }
            for (indiv = 2; indiv < 0x21 && error != 0; indiv++) {
                uint32_t ref = 12000000 / indiv;

                if (ref > 3999999 && ref < 0x7a1201) {
                    for (fbdiv = 2; fbdiv < 0x202; fbdiv++) {
                        uint32_t vco = (ref >> 1) * fbdiv;

                        if (vco > 49999999 && vco < 0x7735941) {
                            uint32_t want = target >> 2;
                            uint32_t diff = (vco < want || vco == want) ? want - vco : vco - want;

                            if (diff < error) {
                                error = diff;
                                best_outdiv = outdiv_sel;
                                best_indiv = indiv;
                                best_fbdiv = fbdiv;
                                if (diff == 0) {
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        CLK_PLLCTL = src_bit | ((best_outdiv - 1) << 14) | ((best_indiv - 1) << 9) | (best_fbdiv - 2);
        CLK_WaitClockReady(CLK_STATUS_PLLSTB);
        result = best_fbdiv * (12000000 / (best_indiv * best_outdiv)) * 2;
    } else {
        CLK_PLLCTL = (source == 0) ? 0x421e : 0x8421e;
        CLK_WaitClockReady(CLK_STATUS_PLLSTB);
        result = CLK_GetPLLClockFreq();
    }
    return result;
}

/* Run the core at about hz (at most 192 MHz) from the PLL. Returns the real core clock. */
uint32_t CLK_SetCoreClock(uint32_t hz)
{
    uint32_t status = CLK_STATUS;
    uint32_t actual;

    if (hz > 192000000) {
        hz = 192000000;
    }
    CLK_PWRCTL |= CLK_PWRCTL_HIRCEN;
    CLK_WaitClockReady(CLK_STATUS_HIRCSTB);
    CLK_CLKSEL0 |= 7;
    CLK_CLKDIV0 &= ~0xfu;
    if ((CLK_PWRCTL & CLK_PWRCTL_HXTEN) == 0) {
        actual = CLK_EnablePLL(PLL_SOURCE_HIRC, hz);
        status = CLK_STATUS;
    } else {
        actual = CLK_EnablePLL(0, hz);
    }
    CLK_SetHCLK(2, 0);
    if ((status & CLK_STATUS_HIRCSTB) == 0) {
        CLK_PWRCTL &= ~CLK_PWRCTL_HIRCEN;
    }
    return actual;
}
