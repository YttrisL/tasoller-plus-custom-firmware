#pragma once
/* What spi.c defines. */

uint32_t SPI_Open(volatile uint32_t *spi,uint32_t mode,uint32_t spi_mode,int bit_width,uint32_t bus_clock);
uint32_t QSPI_Open(uint32_t *qspi,uint32_t mode,uint32_t spi_mode,int bit_width,uint32_t bus_clock);
void SPI_DisableAutoSS(int spi);
