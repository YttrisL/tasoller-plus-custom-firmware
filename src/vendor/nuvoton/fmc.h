#pragma once
/* What fmc.c defines. */

uint32_t FMC_Read(uint32_t address);
void FMC_Write(uint32_t address, uint32_t data);
uint32_t FMC_EraseSPROM(void);
int32_t FMC_Erase(uint32_t address);
int32_t FMC_EraseRange(uint32_t address, uint32_t size);
int32_t FMC_IspRange(uint32_t command, uint32_t address, uint32_t end, uint32_t *buf);
void FMC_ReadRange(uint32_t start, uint32_t end, uint32_t *buf);
void FMC_WriteRange(uint32_t start, uint32_t end, uint32_t *data);
