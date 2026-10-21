/* SPI interfaces (Nuvoton library). QSPI0 and SPI0..3 differ in the clock selection only. */

#include "ss321.h"

uint32_t SPI_Open(volatile uint32_t *spi,uint32_t mode,uint32_t spi_mode,int bit_width,uint32_t bus_clock)
{
  uint32_t hclk;
  uint32_t value;
  uint32_t peripheral_clock;
  
  spi[0x18] = spi[0x18] & 0xfffffffe;
  if (bit_width == 0x20) {
    bit_width = 0;
  }
  hclk = CLK_GetHCLKFreq();
  if (mode == 0) {
    spi[2] = 0;
    *spi = bit_width << 8 | spi_mode | 1;
    if (hclk <= bus_clock) {
      if (spi == (uint32_t *)&SPI0_CTL) {
        value = CLK_CLKSEL2;
        CLK_CLKSEL2 = value & 0xffffffcf | 0x20;
      }
      else if (spi == (uint32_t *)&SPI1_CTL) {
        value = CLK_CLKSEL2;
        CLK_CLKSEL2 = value & 0xffffff3f | 0x80;
      }
      else if (spi == (uint32_t *)&SPI2_CTL) {
        value = CLK_CLKSEL2;
        CLK_CLKSEL2 = value & 0xfffff3ff | 0x800;
      }
      else {
        value = CLK_CLKSEL2;
        CLK_CLKSEL2 = value & 0xffffcfff | 0x2000;
      }
    }
    if (spi == (uint32_t *)&SPI0_CTL) {
      value = CLK_CLKSEL2;
      if ((value & 0x30) == 0) {
        peripheral_clock = 12000000;
      }
      else {
        value = CLK_CLKSEL2;
        if ((value & 0x30) == 0x10) {
          peripheral_clock = CLK_GetPLLClockFreq();
        }
        else {
          value = CLK_CLKSEL2;
          if ((value & 0x30) == 0x20) {
            peripheral_clock = CLK_GetPCLK1Freq();
          }
          else {
            peripheral_clock = 12000000;
          }
        }
      }
    }
    else if (spi == (uint32_t *)&SPI1_CTL) {
      value = CLK_CLKSEL2;
      if ((value & 0xc0) == 0) {
        peripheral_clock = 12000000;
      }
      else {
        value = CLK_CLKSEL2;
        if ((value & 0xc0) == 0x40) {
          peripheral_clock = CLK_GetPLLClockFreq();
        }
        else {
          value = CLK_CLKSEL2;
          if ((value & 0xc0) == 0x80) {
            peripheral_clock = CLK_GetPCLK0Freq();
          }
          else {
            peripheral_clock = 12000000;
          }
        }
      }
    }
    else if (spi == (uint32_t *)&SPI2_CTL) {
      value = CLK_CLKSEL2;
      if ((value & 0xc00) == 0) {
        peripheral_clock = 12000000;
      }
      else {
        value = CLK_CLKSEL2;
        if ((value & 0xc00) == 0x400) {
          peripheral_clock = CLK_GetPLLClockFreq();
        }
        else {
          value = CLK_CLKSEL2;
          if ((value & 0xc00) == 0x800) {
            peripheral_clock = CLK_GetPCLK1Freq();
          }
          else {
            peripheral_clock = 12000000;
          }
        }
      }
    }
    else {
      value = CLK_CLKSEL2;
      if ((value & 0x3000) == 0) {
        peripheral_clock = 12000000;
      }
      else {
        value = CLK_CLKSEL2;
        if ((value & 0x3000) == 0x1000) {
          peripheral_clock = CLK_GetPLLClockFreq();
        }
        else {
          value = CLK_CLKSEL2;
          if ((value & 0x3000) == 0x2000) {
            peripheral_clock = CLK_GetPCLK0Freq();
          }
          else {
            peripheral_clock = 12000000;
          }
        }
      }
    }
    if (bus_clock < hclk) {
      if (bus_clock < peripheral_clock) {
        if (bus_clock == 0) {
          spi[1] = spi[1] | 0x1ff;
          peripheral_clock = peripheral_clock >> 8;
        }
        else {
          hclk = ((peripheral_clock * 10) / bus_clock + 5) / 10;
          value = hclk - 1;
          if (value < 0x100) {
            spi[1] = spi[1] & 0xfffffe00 | value;
            peripheral_clock = peripheral_clock / hclk;
          }
          else {
            spi[1] = spi[1] | 0x1ff;
            peripheral_clock = peripheral_clock >> 8;
          }
        }
      }
      else {
        spi[1] = 0;
      }
    }
    else {
      spi[1] = 0;
    }
  }
  else {
    spi[2] = 0;
    *spi = mode | bit_width << 8 | spi_mode | 1;
    spi[1] = 0;
    if (spi == (uint32_t *)&SPI0_CTL) {
      hclk = CLK_CLKSEL2;
      CLK_CLKSEL2 = hclk & 0xffffffcf | 0x20;
      peripheral_clock = CLK_GetPCLK1Freq();
    }
    else if (spi == (uint32_t *)&SPI1_CTL) {
      hclk = CLK_CLKSEL2;
      CLK_CLKSEL2 = hclk & 0xffffff3f | 0x80;
      peripheral_clock = CLK_GetPCLK0Freq();
    }
    else if (spi == (uint32_t *)&SPI2_CTL) {
      hclk = CLK_CLKSEL2;
      CLK_CLKSEL2 = hclk & 0xfffff3ff | 0x800;
      peripheral_clock = CLK_GetPCLK1Freq();
    }
    else {
      hclk = CLK_CLKSEL2;
      CLK_CLKSEL2 = hclk & 0xffffcfff | 0x2000;
      peripheral_clock = CLK_GetPCLK0Freq();
    }
  }
  return peripheral_clock;
}

uint32_t QSPI_Open(uint32_t *qspi,uint32_t mode,uint32_t spi_mode,int bit_width,uint32_t bus_clock)
{
  uint32_t hclk;
  uint32_t value;
  uint32_t peripheral_clock;
  
  if (bit_width == 0x20) {
    bit_width = 0;
  }
  hclk = CLK_GetHCLKFreq();
  if (mode == 0) {
    qspi[2] = 0;
    *qspi = bit_width << 8 | spi_mode | 1;
    if (hclk <= bus_clock) {
      value = CLK_CLKSEL2;
      CLK_CLKSEL2 = value & 0xfffffff3 | 8;
    }
    value = CLK_CLKSEL2;
    if ((value & 0xc) == 0) {
      peripheral_clock = 12000000;
    }
    else {
      value = CLK_CLKSEL2;
      if ((value & 0xc) == 4) {
        peripheral_clock = CLK_GetPLLClockFreq();
      }
      else {
        value = CLK_CLKSEL2;
        if ((value & 0xc) == 8) {
          peripheral_clock = CLK_GetPCLK0Freq();
        }
        else {
          peripheral_clock = 12000000;
        }
      }
    }
    if (bus_clock < hclk) {
      if (bus_clock < peripheral_clock) {
        if (bus_clock == 0) {
          qspi[1] = qspi[1] | 0x1ff;
          peripheral_clock = peripheral_clock >> 8;
        }
        else {
          hclk = ((peripheral_clock * 10) / bus_clock + 5) / 10;
          value = hclk - 1;
          if (value < 0x100) {
            qspi[1] = qspi[1] & 0xfffffe00 | value;
            peripheral_clock = peripheral_clock / hclk;
          }
          else {
            qspi[1] = qspi[1] | 0x1ff;
            peripheral_clock = peripheral_clock >> 8;
          }
        }
      }
      else {
        qspi[1] = 0;
      }
    }
    else {
      qspi[1] = 0;
    }
  }
  else {
    qspi[2] = 0;
    *qspi = mode | bit_width << 8 | spi_mode | 1;
    qspi[1] = 0;
    hclk = CLK_CLKSEL2;
    CLK_CLKSEL2 = hclk & 0xfffffff3 | 8;
    peripheral_clock = CLK_GetPCLK0Freq();
  }
  return peripheral_clock;
}

void SPI_DisableAutoSS(int spi)
{
  *(volatile uint32_t *)(spi + 8) = *(volatile uint32_t *)(spi + 8) & 0xfffffff6;
  return;
}
